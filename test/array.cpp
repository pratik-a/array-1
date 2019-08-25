#include "array.h"
#include "test.h"

namespace array {

shape<dim<>, dim<>> make_2d_shape(const dim<>& x, const dim<>& y) {
  return {x, y};
}

template <typename T>
using array_1d = array<T, shape<dense_dim<>>>;
template <typename T>
using array_2d = array<T, shape<dense_dim<>, dim<>>>;
template <typename T>
using array_3d = array<T, shape<dense_dim<>, dim<>, dim<>>>;

TEST(array_default_constructor) {
  array_1d<int> a(make_dense_shape(10));
  for (int x = 0; x < 10; x++) {
    ASSERT_EQ(a(x), 0);
  }

  array_2d<int> b(make_dense_shape(7, 3));
  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 7; x++) {
      ASSERT_EQ(b(x, y), 0);
    }
  }

  array_3d<int> c(make_dense_shape(5, 9, 3));
  for (int z = 0; z < 3; z++) {
    for (int y = 0; y < 9; y++) {
      for (int x = 0; x < 5; x++) {
        ASSERT_EQ(c(x, y, z), 0);
      }
    }
  }

  auto sparse_shape = make_shape(dim<>(-2, 5, 2), dim<>(4, 10, 20));
  array<int, shape<dim<>, dim<>>> sparse(sparse_shape);
  for (int y = 4; y < 14; y++) {
    for (int x = -2; x < 3; x++) {
      ASSERT_EQ(sparse(x, y), 0);
    }
  }

  sparse.clear();
  ASSERT(sparse.empty());
  sparse.clear();
}

TEST(array_fill_constructor) {
  array_1d<int> a(make_dense_shape(10), 3);
  for (int x = 0; x < 10; x++) {
    ASSERT_EQ(a(x), 3);
  }

  array_2d<int> b(make_dense_shape(7, 3), 5);
  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 7; x++) {
      ASSERT_EQ(b(x, y), 5);
    }
  }

  array_3d<int> c(make_dense_shape(5, 9, 3), 7);
  for (int z = 0; z < 3; z++) {
    for (int y = 0; y < 9; y++) {
      for (int x = 0; x < 5; x++) {
        ASSERT_EQ(c(x, y, z), 7);
      }
    }
  }

  auto sparse_shape = make_shape(dim<>(-2, 5, 2), dim<>(4, 10, 20));
  array<int, shape<dim<>, dim<>>> sparse(sparse_shape, 13);
  for (int y = 4; y < 14; y++) {
    for (int x = -2; x < 3; x++) {
      ASSERT_EQ(sparse(x, y), 13);
    }
  }
}

TEST(array_fill_assign) {
  array_1d<int> a;
  a.assign(make_dense_shape(10), 3);
  for (int x = 0; x < 10; x++) {
    ASSERT_EQ(a(x), 3);
  }

  array_2d<int> b;
  b.assign(make_dense_shape(7, 3), 5);
  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 7; x++) {
      ASSERT_EQ(b(x, y), 5);
    }
  }

  array_3d<int> c;
  c.assign(make_dense_shape(5, 9, 3), 7);
  for (int z = 0; z < 3; z++) {
    for (int y = 0; y < 9; y++) {
      for (int x = 0; x < 5; x++) {
        ASSERT_EQ(c(x, y, z), 7);
      }
    }
  }

  array<int, shape<dim<>, dim<>>> sparse;
  auto sparse_shape = make_shape(dim<>(-2, 5, 2), dim<>(4, 10, 20));
  sparse.assign(sparse_shape, 13);
  for (int y = 4; y < 14; y++) {
    for (int x = -2; x < 3; x++) {
      ASSERT_EQ(sparse(x, y), 13);
    }
  }
}

TEST(sparse_array) {
  auto sparse_shape = make_shape(dim<>(-2, 5, 2), dim<>(4, 10, 20));
  array<int, shape<dim<>, dim<>>> sparse(sparse_shape);
  // Fill the storage with a constant.
  for (int i = 0; i < sparse_shape.flat_extent(); i++) {
    sparse.data()[i] = 7;
  }
  // Assign a different constant.
  sparse.assign(sparse_shape, 3);

  // Check that we assigned all of the elements of the array.
  for (int y = 4; y < 14; y++) {
    for (int x = -2; x < 3; x++) {
      ASSERT_EQ(sparse(x, y), 3);
    }
  }

  // Check that only the elements of the array were assigned.
  int sevens = 0;
  for (int i = 0; i < sparse_shape.flat_extent(); i++) {
    if (sparse.data()[i] == 7) {
      sevens++;
    }
  }
  ASSERT_EQ(sevens, sparse_shape.flat_extent() - sparse.size());
}


struct lifetime_counter {
  static int default_constructs;
  static int copy_constructs;
  static int move_constructs;
  static int copy_assigns;
  static int move_assigns;
  static int destructs;

  static void reset() {
    default_constructs = 0;
    copy_constructs = 0;
    move_constructs = 0;
    copy_assigns = 0;
    move_assigns = 0;
    destructs = 0;
  }

  static int constructs() {
    return default_constructs + copy_constructs + move_constructs;
  }

  lifetime_counter() { default_constructs++; }
  lifetime_counter(const lifetime_counter&) { copy_constructs++; }
  lifetime_counter(lifetime_counter&&) { move_constructs++; }
  ~lifetime_counter() { destructs++; }

  lifetime_counter& operator=(const lifetime_counter&) { copy_assigns++; return *this; }
  lifetime_counter& operator=(lifetime_counter&&) { move_assigns++; return *this; }
};

int lifetime_counter::default_constructs = 0;
int lifetime_counter::copy_constructs = 0;
int lifetime_counter::move_constructs = 0;
int lifetime_counter::copy_assigns = 0;
int lifetime_counter::move_assigns = 0;
int lifetime_counter::destructs = 0;

typedef shape<dim<>, dim<>> LifetimeShape;
auto lifetime_shape = make_shape(dim<>(-2, 5, 2), dim<>(4, 10, 20));

TEST(array_default_init_lifetime) {
  lifetime_counter::reset();
  {
    array<lifetime_counter, LifetimeShape> default_init(lifetime_shape);
  }
  ASSERT_EQ(lifetime_counter::default_constructs, lifetime_shape.size());
  ASSERT_EQ(lifetime_counter::destructs, lifetime_shape.size());
}

TEST(array_copy_init_lifetime) {
  lifetime_counter::reset();
  {
    array<lifetime_counter, LifetimeShape> copy_init(lifetime_shape, lifetime_counter());
  }
  ASSERT_EQ(lifetime_counter::copy_constructs, lifetime_shape.size());
  ASSERT_EQ(lifetime_counter::destructs, lifetime_shape.size() + 1);
}

TEST(array_copy_lifetime) {
  {
    array<lifetime_counter, LifetimeShape> source(lifetime_shape);
    lifetime_counter::reset();
    array<lifetime_counter, LifetimeShape> copy(source);
  }
  ASSERT_EQ(lifetime_counter::copy_constructs, lifetime_shape.size());
  ASSERT_EQ(lifetime_counter::destructs, lifetime_shape.size() * 2);
}

TEST(array_move_lifetime) {
  {
    array<lifetime_counter, LifetimeShape> source(lifetime_shape);
    lifetime_counter::reset();
    array<lifetime_counter, LifetimeShape> move(std::move(source));
  }
  ASSERT_EQ(lifetime_counter::constructs(), 0);
  ASSERT_EQ(lifetime_counter::destructs, lifetime_shape.size());
}

TEST(array_copy_assign_lifetime) {
  {
    array<lifetime_counter, LifetimeShape> source(lifetime_shape);
    lifetime_counter::reset();
    array<lifetime_counter, LifetimeShape> assign;
    assign = source;
  }
  ASSERT_EQ(lifetime_counter::copy_constructs, lifetime_shape.size());
  ASSERT_EQ(lifetime_counter::destructs, lifetime_shape.size() * 2);
}

TEST(array_move_assign_lifetime) {
  {
    array<lifetime_counter, LifetimeShape> source(lifetime_shape);
    lifetime_counter::reset();
    array<lifetime_counter, LifetimeShape> assign;
    assign = std::move(source);
  }
  ASSERT_EQ(lifetime_counter::copy_constructs, 0);
  ASSERT_EQ(lifetime_counter::destructs, lifetime_shape.size());
}

TEST(array_clear_lifetime) {
  lifetime_counter::reset();
  array<lifetime_counter, LifetimeShape> default_init(lifetime_shape);
  default_init.clear();
  ASSERT_EQ(lifetime_counter::default_constructs, lifetime_shape.size());
  ASSERT_EQ(lifetime_counter::destructs, lifetime_shape.size());
}

}  // namespace array
