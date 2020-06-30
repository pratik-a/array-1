// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "array.h"
#include "benchmark.h"

#include <random>
#include <iostream>

using namespace nda;

// The standard matrix notation is to refer to elements by 'row,
// column'. To make this efficient for typical programs, we're going
// to make the second dimension the dense dim. This shape has the
// option of making the size of the matrix compile-time constant via
// the template parameters.
template <index_t Rows = UNK, index_t Cols = UNK>
using matrix_shape = shape<dim<UNK, Rows>, dense_dim<UNK, Cols>>;

// A matrix or matrix_ref is an array or array_ref with Shape =
// matrix_shape.
template <typename T, index_t Rows = UNK, index_t Cols = UNK,
          typename Alloc = std::allocator<T>>
using matrix = array<T, matrix_shape<Rows, Cols>, Alloc>;
template <typename T, index_t Rows = UNK, index_t Cols = UNK>
using matrix_ref = array_ref<T, matrix_shape<Rows, Cols>>;

// A textbook implementation of matrix multiplication. This is very simple,
// but it is slow, primarily because of poor locality of the loads of b. The
// reduction loop is innermost.
template <typename TAB, typename TC, index_t Rows, index_t Cols>
__attribute__((noinline))
void multiply_reduce_cols(const matrix_ref<TAB>& a, const matrix_ref<TAB>& b,
                          const matrix_ref<TC, Rows, Cols>& c) {
  for (index_t i : c.i()) {
    for (index_t j : c.j()) {
      c(i, j) = 0;
      for (index_t k : a.j()) {
        c(i, j) += a(i, k) * b(k, j);
      }
    }
  }
}

// This implementation moves the reduction loop between the rows and columns
// loops. This avoids the locality problem for the loads from b. This also is
// an easier loop to vectorize (it does not vectorize a reduction variable).
template <typename TAB, typename TC, index_t Rows, index_t Cols>
__attribute__((noinline))
void multiply_reduce_rows(const matrix_ref<TAB>& a, const matrix_ref<TAB>& b,
                          const matrix_ref<TC, Rows, Cols>& c) {
  for (index_t i : c.i()) {
    for (index_t j : c.j()) {
      c(i, j) = 0;
    }
    for (index_t k : a.j()) {
      for (index_t j : c.j()) {
        c(i, j) += a(i, k) * b(k, j);
      }
    }
  }
}

// This implementation reorders the reduction loop innermost. This vectorizes
// well, but has poor locality. However, this will be a useful helper function
// for the tiled implementation below.
template <typename TAB, typename TC, index_t Rows, index_t Cols>
__attribute__((always_inline))
void multiply_reduce_matrices(const matrix_ref<TAB>& a, const matrix_ref<TAB>& b,
                              const matrix_ref<TC, Rows, Cols>& c) {
  for (index_t i : c.i()) {
    for (index_t j : c.j()) {
      c(i, j) = 0;
    }
  }
  for (index_t k : a.j()) {
    for (index_t i : c.i()) {
      for (index_t j : c.j()) {
        c(i, j) += a(i, k) * b(k, j);
      }
    }
  }
}

// This implementation of matrix multiplication splits the loops over
// the output matrix into chunks, and reorders the small loops
// innermost to form tiles. This implementation should allow the compiler
// to keep all of the accumulators for the output in registers.
// For the matrix size benchmarked in main, this runs in ~0.44ms.
// This appears to achieve ~70% of the peak theoretical throughput
// of my machine.
template <typename TAB, typename TC>
__attribute__((noinline))
void multiply_reduce_tiles(const matrix_ref<TAB>& a, const matrix_ref<TAB>& b,
                           const matrix_ref<TC>& c) {
  // Adjust this depending on the target architecture. For AVX2,
  // vectors are 256-bit.
  constexpr index_t vector_size = 32 / sizeof(TC);

  // We want the tiles to be as big as possible without spilling any
  // of the accumulator registers to the stack.
  constexpr index_t tile_rows = 3;
  constexpr index_t tile_cols = vector_size * 4;

  for (auto io : split<tile_rows>(c.i())) {
    for (auto jo : split<tile_cols>(c.j())) {
      // Make a reference to this tile of the output.
      auto c_tile = c(io, jo);
#if 0
      // TODO: This should work, but it's slow, probably due to the
      // absence of __restrict in array/array_ref
      // (https://bugs.llvm.org/show_bug.cgi?id=45863)
      multiply_reduce_matrices(a, b, c_tile);
#elif 0
      // TODO: This should work, but it's slow, probably due to the
      // absence of __restrict in array/array_ref
      // (https://bugs.llvm.org/show_bug.cgi?id=45863)
      for (index_t i : c_tile.i()) {
        for (index_t j : c_tile.j()) {
          c_tile(i, j) = 0;
        }
      }
      for (index_t k : a.j()) {
        for (index_t i : c_tile.i()) {
          for (index_t j : c_tile.j()) {
            c_tile(i, j) += a(i, k) * b(k, j);
          }
        }
      }
#else
      TC buffer[tile_rows * tile_cols] = { 0.0f };
      matrix_ref<TC, tile_rows, tile_cols> accumulator(buffer, make_compact(c_tile.shape()));
      for (index_t k : a.j()) {
        for (index_t i : c_tile.i()) {
          for (index_t j : c_tile.j()) {
            accumulator(i, j) += a(i, k) * b(k, j);
          }
        }
      }
#if 0
      // TODO: This should work, but it's slow, it appears to
      // blow up the nice in-register accumulation of the loop
      // above.
      copy(accumulator, c_tile);
#else
      for (index_t i : c_tile.i()) {
        for (index_t j : c_tile.j()) {
          c_tile(i, j) = accumulator(i, j);
        }
      }
#endif
#endif
    }
  }
}

int main(int, const char**) {
  // Define two input matrices.
  constexpr index_t M = 24;
  constexpr index_t K = 10000;
  constexpr index_t N = 64;
  matrix<float> a({M, K});
  matrix<float> b({K, N});

  // 'for_each_value' calls the given function with a reference to
  // each value in the array. Use this to randomly initialize the
  // matrices with random values.
  std::mt19937_64 rng;
  std::uniform_real_distribution<float> uniform(0, 1);
  a.for_each_value([&](float& x) { x = uniform(rng); });
  b.for_each_value([&](float& x) { x = uniform(rng); });

  // Compute the result using all matrix multiply methods.
  matrix<float> c_reduce_cols({M, N});
  double reduce_cols_time = benchmark([&]() {
    multiply_reduce_cols(a.ref(), b.ref(), c_reduce_cols.ref());
  });
  std::cout << "reduce_cols time: " << reduce_cols_time * 1e3 << " ms" << std::endl;

  matrix<float> c_reduce_rows({M, N});
  double reduce_rows_time = benchmark([&]() {
    multiply_reduce_rows(a.ref(), b.ref(), c_reduce_rows.ref());
  });
  std::cout << "reduce_rows time: " << reduce_rows_time * 1e3 << " ms" << std::endl;

  matrix<float> c_reduce_tiles({M, N});
  double reduce_tiles_time = benchmark([&]() {
    multiply_reduce_tiles(a.ref(), b.ref(), c_reduce_tiles.ref());
  });
  std::cout << "reduce_tiles time: " << reduce_tiles_time * 1e3 << " ms" << std::endl;

  // Verify the results from all methods are equal.
  const float epsilon = 1e-4f;
  for (index_t i = 0; i < M; i++) {
    for (index_t j = 0; j < N; j++) {
      if (std::abs(c_reduce_rows(i, j) - c_reduce_cols(i, j)) > epsilon) {
        std::cout
          << "c_reduce_rows(" << i << ", " << j << ") = " << c_reduce_rows(i, j)
          << " != c_reduce_cols(" << i << ", " << j << ") = " << c_reduce_cols(i, j) << std::endl;
        return -1;
      }
      if (std::abs(c_reduce_tiles(i, j) - c_reduce_cols(i, j)) > epsilon) {
        std::cout
          << "c_reduce_tiles(" << i << ", " << j << ") = " << c_reduce_tiles(i, j)
          << " != c_reduce_cols(" << i << ", " << j << ") = " << c_reduce_cols(i, j) << std::endl;
        return -1;
      }
    }
  }
  return 0;
}

