/*
utils.c - Random C utilities to make it more bearable to work with.
Made for my usecase.

Tested with C23 only.

Some of these come from other people licensed in the public domain, some I wrote myself.

Documentation is included at the end of the file.

Licensed in the public domain. Do whatever you want with it.

Styleguide:
- 2 space indentation,
- pointers aligned to the type (int* ptr, not int *ptr),
- use snake_case for functions, function-like macros, variables and types,
- use ALL_CAPS for macro expansions and macro constants,
- use shorthands defined below always.
*/

#ifndef _UTILS_C
#define _UTILS_C

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

typedef float    f32;
typedef double   f64;

typedef size_t   usz;
typedef ptrdiff_t isz;

#define u8_max  UINT8_MAX
#define u16_max UINT16_MAX
#define u32_max UINT32_MAX
#define u64_max UINT64_MAX

#define i8_min  INT8_MIN
#define i8_max  INT8_MAX
#define i16_min INT16_MIN
#define i16_max INT16_MAX
#define i32_min INT32_MIN
#define i32_max INT32_MAX
#define i64_min INT64_MIN
#define i64_max INT64_MAX

#define nil NULL

#define _int_by_1_5(val) \
  ((val) + (val) / 2)

#ifdef USE_RANDOM_UTIL

#include <sys/random.h>
#include <errno.h>
#include <assert.h>

// <@
// @name random_u64
// @kind function
// @return A random u64 value.
inline u64 random_u64(void) {
// @>
  u64 value = 0;
  usz offset = 0;

  while (offset < sizeof(value)) {
    ssize_t result = getrandom(
      ((u8*)&value) + offset,
      sizeof(value) - offset,
      0
    );

    if (result <= 0) {
      if (errno == EINTR) {
        continue;
      }

      assert(0 && "getrandom failed");
    }

    offset += (usz)result;
  }

  return value;
}

// <@
// @name random_i64
// @kind function
// @return A random i64 value.
inline i64 random_i64(void) {
// @>
  return (i64)random_u64();
}

// <@
// @name random_u32
// @kind function
// @return A random u32 value.
inline u32 random_u32(void) {
// @>
  return (u32)random_u64();
}

// <@
// @name random_i32
// @kind function
// @return A random i32 value.
inline i32 random_i32(void) {
// @>
  return (i32)random_u32();
}

// <@
// @name random_u16
// @kind function
// @return A random u16 value.
inline u16 random_u16(void) {
// @>
  return (u16)random_u64();
}

// <@
// @name random_i16
// @kind function
// @return A random i16 value.
inline i16 random_i16(void) {
// @>
  return (i16)random_u16();
}

// <@
// @name random_u8
// @kind function
// @return A random u8 value.
inline u8 random_u8(void) {
// @>
  return (u8)random_u64();
}

// <@
// @name random_i8
// @kind function
// @return A random i8 value.
inline i8 random_i8(void) {
// @>
  return (i8)random_u8();
}

// <@
// @name random_u64_range
// @kind function
// @desc Returns a random u64 value in the range [min, max]. If min > max, the values are swapped.
// @param min The minimum value of the range (inclusive).
// @param max The maximum value of the range (inclusive).
// @return A random u64 value in the specified range.
inline u64 random_u64_range(u64 min, u64 max) {
// @>
  if (min > max) {
    u64 tmp = min;
    min = max;
    max = tmp;
  }

  if (min == 0 && max == u64_max) {
    return random_u64();
  }

  u64 range = max - min + 1;

  u64 limit = u64_max - (u64_max % range);

  u64 value;

  do {
    value = random_u64();
  } while (value >= limit);

  return min + (value % range);
}

// <@
// @name random_i64_range
// @kind function
// @desc Returns a random i64 value in the range [min, max]. If min > max, the values are swapped.
// @param min The minimum value of the range (inclusive).
// @param max The maximum value of the range (inclusive).
// @return A random i64 value in the specified range.
inline i64 random_i64_range(i64 min, i64 max) {
// @>
  if (min > max) {
    i64 tmp = min;
    min = max;
    max = tmp;
  }

  u64 range = (u64)max - (u64)min + 1;

  u64 limit = u64_max - (u64_max % range);

  u64 value;

  do {
    value = random_u64();
  } while (value >= limit);

  return min + (i64)(value % range);
}

// <@
// @name random_u32_range
// @kind function
// @desc Returns a random u32 value in the range [min, max]. If min > max, the values are swapped.
// @param min The minimum value of the range (inclusive).
// @param max The maximum value of the range (inclusive).
// @return A random u32 value in the specified range.
inline u32 random_u32_range(u32 min, u32 max) {
// @>
  return (u32)random_u64_range(min, max);
}

// <@
// @name random_i32_range
// @kind function
// @desc Returns a random i32 value in the range [min, max]. If min > max, the values are swapped.
// @param min The minimum value of the range (inclusive).
// @param max The maximum value of the range (inclusive).
// @return A random i32 value in the specified range.
inline i32 random_i32_range(i32 min, i32 max) {
// @>
  return (i32)random_i64_range(min, max);
}

// <@
// @name random_u16_range
// @kind function
// @desc Returns a random u16 value in the range [min, max]. If min > max, the values are swapped.
// @param min The minimum value of the range (inclusive).
// @param max The maximum value of the range (inclusive).
// @return A random u16 value in the specified range.
inline u16 random_u16_range(u16 min, u16 max) {
// @>
  return (u16)random_u64_range(min, max);
}

// <@
// @name random_i16_range
// @kind function
// @desc Returns a random i16 value in the range [min, max]. If min > max, the values are swapped.
// @param min The minimum value of the range (inclusive).
// @param max The maximum value of the range (inclusive).
// @return A random i16 value in the specified range.
inline i16 random_i16_range(i16 min, i16 max) {
// @>
  return (i16)random_i64_range(min, max);
}

// <@
// @name random_u8_range
// @kind function
// @desc Returns a random u8 value in the range [min, max]. If min > max, the values are swapped.
// @param min The minimum value of the range (inclusive).
// @param max The maximum value of the range (inclusive).
// @return A random u8 value in the specified range.
inline u8 random_u8_range(u8 min, u8 max) {
// @>
  return (u8)random_u64_range(min, max);
}

// <@
// @name random_i8_range
// @kind function
// @desc Returns a random i8 value in the range [min, max]. If min > max, the values are swapped.
// @param min The minimum value of the range (inclusive).
// @param max The maximum value of the range (inclusive).
// @return A random i8 value in the specified range.
inline i8 random_i8_range(i8 min, i8 max) {
// @>
  return (i8)random_i64_range(min, max);
}

// <@
// @name random_f64
// @kind function
// @return A random f64 value in the range [0.0, 1.0).
inline f64 random_f64(void) {
// @>
  return (f64)random_u64() / ((f64)u64_max + 1.0);
}

// <@
// @name random_f32
// @kind function
// @return A random f32 value in the range [0.0f, 1.0f).
inline f32 random_f32(void) {
// @>
  return (f32)random_u32() / ((f32)u32_max + 1.0f);
}

// <@
// @name random_f64_range
// @kind function
// @desc Returns a random f64 value in the range [min, max). If min > max, the values are swapped.
// @param min The minimum value of the range (inclusive).
// @param max The maximum value of the range (exclusive).
// @return A random f64 value in the specified range.
inline f64 random_f64_range(f64 min, f64 max) {
// @>
  return min + (max - min) * random_f64();
}

// <@
// @name random_f32_range
// @kind function
// @desc Returns a random f32 value in the range [min, max). If min > max, the values are swapped.
// @param min The minimum value of the range (inclusive).
// @param max The maximum value of the range (exclusive).
// @return A random f32 value in the specified range.
inline f32 random_f32_range(f32 min, f32 max) {
// @>
  return min + (max - min) * random_f32();
}

#endif // USE_RANDOM_UTIL



#ifdef USE_ALLOC_UTIL

#include <string.h>
#include <assert.h>

typedef struct allocator allocator;

// <@
// @name allocator
// @kind type
// @desc A struct that represents a memory allocator. It contains function pointers for allocating,
// reallocating and freeing memory, as well as a context pointer that can be used to store any state the allocator needs.
// @field ctx A pointer to any state the allocator needs. This is passed to the alloc, realloc and free functions.
// @field alloc A function pointer to a function that allocates memory.
// @field realloc A function pointer to a function that reallocates memory.
// @field free A function pointer to a function that frees memory.
struct allocator {
  void* ctx;

  void* (*alloc)(
    void* ctx,
    usz size
  );

  void* (*realloc)(
    void* ctx,
    void* ptr,
    usz new_size
  );

  void (*free)(
    void* ctx,
    void* ptr
  );
};
// @>

void* _libc_alloc(
  void* ctx,
  usz size
) {
  (void)ctx;
  return malloc(size);
}

void* _libc_realloc(
  void* ctx,
  void* ptr,
  usz new_size
) {
  (void)ctx;

  return realloc(
    ptr,
    new_size
  );
}

void _libc_free(
  void* ctx,
  void* ptr
) {
  (void)ctx;
  free(ptr);
}

// <@
// @name libc_allocator
// @kind function
// @return An allocator that uses the C standard library's malloc, realloc and free functions.
allocator libc_allocator(void) {
// @>
  return (allocator) {
    .ctx = nil,
    .alloc = _libc_alloc,
    .realloc = _libc_realloc,
    .free = _libc_free,
  };
}

// <@
// @name alloc_tracker
// @kind type
// @desc A struct that can be used to track allocations made by an allocator.
// It contains a dynamic array of pointers to the allocated memory, as well as the count and capacity of the array.
// This can be used to free all allocated memory at once, or to check if a pointer was allocated by the allocator.
// @warning This allocator is not thread-safe and should only be used in a single-threaded context.
// @field allocations A dynamic array of pointers to the allocated memory.
// @field count The number of allocated pointers currently being tracked.
// @field capacity The capacity of the allocations array.
typedef struct {
  void** allocations;
  usz count;
  usz capacity;
} alloc_tracker;
// @>

bool _alloc_tracker_resize(
  alloc_tracker* tracker
) {
  usz new_capacity =
    _int_by_1_5(tracker->capacity);

  void** new_allocations =
    realloc(
      tracker->allocations,
      sizeof(void*) * new_capacity
    );

  if (new_allocations == nil) {
    return false;
  }

  tracker->allocations = new_allocations;
  tracker->capacity = new_capacity;

  return true;
}

// <@
// @name alloc_tracker_track_ptr
// @kind function
// @desc Tracks a pointer in the alloc_tracker. This should be called whenever memory is allocated using the tracked allocator.
// @return true if the pointer was successfully tracked, false if there was an error (e.g. out of memory).
// @param tracker The alloc_tracker to track the pointer in.
// @param ptr The pointer to track.
bool alloc_tracker_track_ptr(
  alloc_tracker* tracker,
  void* ptr
) {
// @>
  if (ptr == nil) {
    return false;
  }

  if (tracker->allocations == nil) {
    tracker->capacity = 16;

    tracker->allocations =
      malloc(
        sizeof(void*) *
        tracker->capacity
      );

    if (tracker->allocations == nil) {
      tracker->capacity = 0;
      return false;
    }
  }

  if (tracker->count >= tracker->capacity) {
    if (!_alloc_tracker_resize(tracker)) {
      return false;
    }
  }

  tracker->allocations[
    tracker->count++
  ] = ptr;

  return true;
}

// <@
// @name alloc_tracker_untrack_ptr
// @kind function
// @desc Untracks a pointer in the alloc_tracker.
// @param tracker The alloc_tracker to untrack the pointer from.
// @param ptr The pointer to untrack.
void alloc_tracker_untrack_ptr(
  alloc_tracker* tracker,
  void* ptr
) {
// @>
  for (usz i = 0; i < tracker->count; i++) {
    if (tracker->allocations[i] == ptr) {
      tracker->allocations[i] =
        tracker->allocations[
          tracker->count - 1
        ];

      tracker->count -= 1;

      return;
    }
  }
}

void* _tracked_alloc(
  void* ctx,
  usz size
) {
  alloc_tracker* tracker = ctx;

  void* ptr = malloc(size);

  if (ptr == nil) {
    return nil;
  }

  if (
    !alloc_tracker_track_ptr(
      tracker,
      ptr
    )
  ) {
    free(ptr);
    return nil;
  }

  return ptr;
}

void* _tracked_realloc(
  void* ctx,
  void* ptr,
  usz new_size
) {
  alloc_tracker* tracker = ctx;

  if (ptr == nil) {
    void* new_ptr =
      malloc(new_size);

    if (new_ptr == nil) {
      return nil;
    }

    if (
      !alloc_tracker_track_ptr(
        tracker,
        new_ptr
      )
    ) {
      free(new_ptr);
      return nil;
    }

    return new_ptr;
  }

  for (usz i = 0; i < tracker->count; i++) {
    if (
      tracker->allocations[i] ==
      ptr
    ) {
      void* new_ptr =
        realloc(
          ptr,
          new_size
        );

      if (new_ptr == nil) {
        return nil;
      }

      tracker->allocations[i] =
        new_ptr;

      return new_ptr;
    }
  }

  return nil;
}

void _tracked_free(
  void* ctx,
  void* ptr
) {
  alloc_tracker* tracker = ctx;

  alloc_tracker_untrack_ptr(
    tracker,
    ptr
  );

  free(ptr);
}

// <@
// @name tracked_allocator
// @kind function
// @param tracker The alloc_tracker to track the allocated pointers in.
// @return An allocator that tracks all allocated pointers in the given alloc_tracker.
allocator tracked_allocator(
  alloc_tracker* tracker
) {
// @>
  return (allocator) {
    .ctx = tracker,
    .alloc = _tracked_alloc,
    .realloc = _tracked_realloc,
    .free = _tracked_free,
  };
}

// <@
// @name alloc_tracker_free_all
// @kind function
// @desc Frees all pointers currently being tracked by the alloc_tracker, and resets the tracker to an empty state.
// This should be used to free all memory allocated by a tracked allocator at once.
// @param tracker The alloc_tracker to free all tracked pointers from.
void alloc_tracker_free_all(
  alloc_tracker* tracker
) {
// @>
  for (usz i = 0; i < tracker->count; i++) {
    free(
      tracker->allocations[i]
    );
  }

  free(tracker->allocations);

  tracker->allocations = nil;
  tracker->count = 0;
  tracker->capacity = 0;
}

#ifndef ARENA_ALIGNMENT
#define ARENA_ALIGNMENT 8
#endif

typedef struct arena_chunk {
  u8* memory;
  usz capacity;
  usz offset;
  struct arena_chunk* next;
} _arena_chunk;

typedef struct {
  _arena_chunk* head;
  _arena_chunk* tail;
  _arena_chunk* current;
  usz chunk_size;
} _arena_class;

// <@
// @name arena
// @kind type
// @desc A struct that represents a memory arena allocator.
// @note All fiels are private and should not be accessed directly. Use the provided functions to interact with the arena.
// @>
typedef struct {
  _arena_class primary;
  _arena_class oversized;
} arena;

_Static_assert(
  (ARENA_ALIGNMENT &
  (ARENA_ALIGNMENT - 1)) == 0,
  "ARENA_ALIGNMENT must be a power of two"
);

usz _arena_align(
  usz x
) {
  usz mask =
    ARENA_ALIGNMENT - 1;

  return (x + mask) & ~mask;
}

_arena_chunk* _arena_chunk_create(
  usz capacity
) {
  _arena_chunk* c =
    malloc(sizeof(_arena_chunk));

  if (c == nil) {
    return nil;
  }

  c->memory =
    malloc(capacity);

  if (c->memory == nil) {
    free(c);
    return nil;
  }

  c->capacity = capacity;
  c->offset = 0;
  c->next = nil;

  return c;
}

void _arena_class_init(
  _arena_class* cls,
  usz chunk_size
) {
  cls->head = nil;
  cls->tail = nil;
  cls->current = nil;
  cls->chunk_size = chunk_size;
}

void _arena_class_destroy(
  _arena_class* cls
) {
  _arena_chunk* c =
    cls->head;

  while (c != nil) {
    _arena_chunk* next =
      c->next;

    free(c->memory);
    free(c);

    c = next;
  }

  cls->head = nil;
  cls->tail = nil;
  cls->current = nil;
}

void _arena_class_reset(
  _arena_class* cls
) {
  for (
    _arena_chunk* c = cls->head;
    c != nil;
    c = c->next
  ) {
    c->offset = 0;
  }

  cls->current = cls->head;
}

void* _arena_class_alloc(
  _arena_class* cls,
  usz size
) {
  size = _arena_align(size);

  if (cls->current == nil) {
    cls->current = cls->head;
  }

  while (
    cls->current != nil &&
    cls->current->offset +
    size >
    cls->current->capacity
  ) {
    cls->current =
      cls->current->next;
  }

  if (cls->current == nil) {
    usz alloc_size =
      size > cls->chunk_size
      ? size
      : cls->chunk_size;

    _arena_chunk* chunk =
      _arena_chunk_create(
        alloc_size
      );

    if (chunk == nil) {
      return nil;
    }

    if (cls->head == nil) {
      cls->head = chunk;
      cls->tail = chunk;
    } else {
      cls->tail->next =
        chunk;

      cls->tail = chunk;
    }

    cls->current = chunk;
  }

  void* ptr =
    cls->current->memory +
    cls->current->offset;

  cls->current->offset +=
    size;

  return ptr;
}

// <@
// @name arena_init_custom
// @kind function
// @desc Initializes an arena with a custom chunk size. The chunk size determines how much memory is allocated at once when the arena needs to grow.
// @param a The arena to initialize.
// @param chunk_size The chunk size to use for the arena. This should be a multiple of ARENA_ALIGNMENT. If not specified, it defaults to 64 KB.
// @return true if the arena was successfully initialized, false if there was an error (e.g. out of memory).
bool arena_init_custom(
  arena* a,
  usz chunk_size
) {
// @>
  if (a == nil) {
    return false;
  }

  _arena_class_init(
    &a->primary,
    chunk_size
  );

  _arena_class_init(
    &a->oversized,
    chunk_size
  );

  return true;
}

// <@
// @name arena_init
// @kind function
// @desc Initializes an arena with the default chunk size (64 KB). The chunk size determines how much memory is allocated at once when the arena needs to grow.
// @param a The arena to initialize.
// @return true if the arena was successfully initialized, false if there was an error (e.g. out of memory).
bool arena_init(
  arena* a
) {
// @>
  return arena_init_custom(
    a,
    64 * 1024
  );
}

// <@
// @name arena_reset
// @kind function
// @desc Resets the arena allocator, making all memory allocated from the primary arena reusable.
// Oversized allocations are freed immediately. Existing pointers allocated from the arena become invalid after this call.
// @param a The arena to reset.
void arena_reset(
  arena* a
) {
// @>
  _arena_class_reset(
    &a->primary
  );

  _arena_class_destroy(
    &a->oversized
  );

  _arena_class_init(
    &a->oversized,
    a->primary.chunk_size
  );
}

// <@
// @name arena_destroy
// @kind function
// @desc Frees all memory owned by the arena, including primary and oversized allocations.
// All pointers allocated from the arena become invalid after this call.
// @param a The arena to destroy.
void arena_destroy(
  arena* a
) {
// @>
  _arena_class_destroy(
    &a->primary
  );

  _arena_class_destroy(
    &a->oversized
  );
}

// <@
// @name arena_alloc
// @kind function
// @desc Allocates memory from the arena.
// Allocations smaller than or equal to the arena chunk size are served from the primary arena.
// Larger allocations are stored separately as oversized allocations.
// @param a The arena to allocate memory from.
// @param size The number of bytes to allocate.
// @return A pointer to the allocated memory, or nil if allocation failed.
void* arena_alloc(
  arena* a,
  usz size
) {
// @>
  size = _arena_align(size);

  if (
    size <=
    a->primary.chunk_size
  ) {
    return _arena_class_alloc(
      &a->primary,
      size
    );
  }

  return _arena_class_alloc(
    &a->oversized,
    size
  );
}

void* _arena_alloc(
  void* ctx,
  usz size
) {
  return arena_alloc(
    (arena*)ctx,
    size
  );
}

void* _arena_realloc(
  void* ctx,
  void* ptr,
  usz new_size
) {
  (void)ctx;
  (void)ptr;
  (void)new_size;
  assert(0 && "arena does not support realloc. use libc allocator or tracked allocator if you need realloc support");
}

void _arena_free(
  void* ctx,
  void* ptr
) {
  (void)ctx;
  (void)ptr;
}


// <@
// @name arena_allocator
// @kind function
// @desc Creates an allocator interface backed by the arena allocator.
// @warning realloc is not supported by this allocator and will assert if used.
// @note free is a no-op; memory is reclaimed only when the arena is reset or destroyed.
// @param a The arena to use for allocations.
// @return An allocator that allocates memory from the arena.
allocator arena_allocator(
  arena* a
) {
// @>
  return (allocator) {
    .ctx = a,
    .alloc = _arena_alloc,
    .realloc = _arena_realloc,
    .free = _arena_free,
  };
}

#endif // USE_ALLOC_UTIL



#ifdef USE_DEFER_UTIL


#if defined(__clangd__)

// we want clangd lsp to typecheck the code but not error because we use nexted funcs
// obviously, this is not correct, because code would run immediately, but because it's
// just the lsp and not the actual compiler, it's fine
#define defer(code) code

#elif defined(__GNUC__)

#define _CONCAT_INTERNAL(x, y) x##y
#define _CONCAT(x, y) _CONCAT_INTERNAL(x, y)

#define _DEFER_INTERNAL(id, code)                     \
  void _CONCAT(_defer_func_, id)(void* _unused) {     \
    (void)_unused;                                    \
    code                                              \
  }                                                   \
  \
  __attribute__((cleanup(_CONCAT(_defer_func_, id)))) \
  int _CONCAT(_defer_var_, id) = 0

// <@
// @name defer
// @kind macro
// @desc Schedules the given code to be executed when the current scope is exited.
// This is useful for ensuring that resources are properly released, even if an error occurs or a return statement is hit.
// @param code Statement or block of code to execute when the current scope is exited.
// @>
#define defer(code) _DEFER_INTERNAL(__COUNTER__, code)

#else

#define defer(...) \
  _Static_assert(0, "defer is only supported with GCC that has nested functions support enabled")

#endif

#endif // USE_DEFER_UTIL



#ifdef USE_STR_VIEW_UTIL

/*
Taken from tsoding's nob.h
*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>

// <@
// @name str_view
// @kind type
// @desc A non-owning view into a string.
// The string is not guaranteed to be null-terminated.
// @field count The length of the string view in bytes.
// @field data A pointer to the string data.
typedef struct {
  size_t count;
  const char *data;
} str_view;
// @>

// Forward declarations so that the functions can call each other
str_view str_view_chop_while(str_view *sv, int (*p)(int x));
str_view str_view_chop_by_delim(str_view *sv, char delim);
str_view str_view_chop_left(str_view *sv, size_t n);
str_view str_view_chop_right(str_view *sv, size_t n);
bool str_view_chop_prefix(str_view *sv, str_view prefix);
bool str_view_chop_suffix(str_view *sv, str_view suffix);
str_view str_view_trim(str_view sv);
str_view str_view_trim_left(str_view sv);
str_view str_view_trim_right(str_view sv);
bool str_view_eq(str_view a, str_view b);
bool str_view_ends_with_cstr(str_view sv, const char *cstr);
bool str_view_ends_with(str_view sv, str_view suffix);
bool str_view_starts_with(str_view sv, str_view prefix);
str_view str_view_from_cstr(const char *cstr);
str_view str_view_from_parts(const char *data, size_t count);

// <@
// @name svpfmt
// @kind macro
// @desc printf format string helper for printing str_view values with printf-style functions.
// @example printf(svpfmt, svpfarg(sv));
// @see_also svpfarg
// @>
#define svpfmt "%.*s"
// <@
// @name svpfarg
// @kind macro
// @desc Expands a str_view into printf arguments compatible with svpfmt.
// @param sv The str_view to print.
// @see_also svpfmt
// @>
#define svpfarg(sv) (int)(sv).count, (sv).data

// <@
// @name str_view_chop_while
// @kind function
// @desc Removes and returns the longest prefix of the string view for which the predicate returns true.
// The original string view is modified to exclude the returned prefix.
// @param sv The string view to chop from.
// @param p A predicate function that returns non-zero for matching characters.
// @return The chopped prefix.
str_view str_view_chop_while(str_view *sv, int (*p)(int x)) {
// @>
  size_t i = 0;
  while (i < sv->count && p(sv->data[i])) {
    i += 1;
  }

  str_view result = str_view_from_parts(sv->data, i);
  sv->count -= i;
  sv->data  += i;

  return result;
}

// @name str_view_chop_by_delim
// @kind function
// @desc Removes and returns everything before the first occurrence of the delimiter.
// The delimiter itself is also removed from the original string view.
// If the delimiter is not found, the entire string view is returned.
// @param sv The string view to chop from.
// @param delim The delimiter character.
// @return The chopped substring.
str_view str_view_chop_by_delim(str_view *sv, char delim) {
// @>
  size_t i = 0;
  while (i < sv->count && sv->data[i] != delim) {
    i += 1;
  }

  str_view result = str_view_from_parts(sv->data, i);

  if (i < sv->count) {
    sv->count -= i + 1;
    sv->data  += i + 1;
  } else {
    sv->count -= i;
    sv->data  += i;
  }

  return result;
}

// <@
// @name str_view_chop_prefix
// @kind function
// @desc Removes a prefix from the string view if it matches.
// @param sv The string view to modify.
// @param prefix The prefix to remove.
// @return true if the prefix matched and was removed, false otherwise.
bool str_view_chop_prefix(str_view *sv, str_view prefix) {
// @>
  if (str_view_starts_with(*sv, prefix)) {
    str_view_chop_left(sv, prefix.count);
    return true;
  }
  return false;
}

// <@
// @name str_view_chop_suffix
// @kind function
// @desc Removes a suffix from the string view if it matches.
// @param sv The string view to modify.
// @param suffix The suffix to remove.
// @return true if the suffix matched and was removed, false otherwise.
bool str_view_chop_suffix(str_view *sv, str_view suffix) {
// @>
  if (str_view_ends_with(*sv, suffix)) {
    str_view_chop_right(sv, suffix.count);
    return true;
  }
  return false;
}

// <@
// @name str_view_chop_left
// @kind function
// @desc Removes and returns up to n bytes from the start of the string view.
// @param sv The string view to chop from.
// @param n The maximum number of bytes to remove.
// @return The removed prefix.
str_view str_view_chop_left(str_view *sv, size_t n) {
// @>
  if (n > sv->count) {
    n = sv->count;
  }

  str_view result = str_view_from_parts(sv->data, n);

  sv->data  += n;
  sv->count -= n;

  return result;
}

// <@
// @name str_view_chop_right
// @kind function
// @desc Removes and returns up to n bytes from the end of the string view.
// @param sv The string view to chop from.
// @param n The maximum number of bytes to remove.
// @return The removed suffix.
str_view str_view_chop_right(str_view *sv, size_t n) {
// @>
  if (n > sv->count) {
    n = sv->count;
  }

  str_view result = str_view_from_parts(sv->data + sv->count - n, n);

  sv->count -= n;

  return result;
}

// <@
// @name str_view_from_parts
// @kind function
// @desc Creates a str_view from a pointer and a length.
// @param data The string data pointer.
// @param count The number of bytes in the string view.
// @return A new str_view.
str_view str_view_from_parts(const char *data, size_t count) {
// @>
  str_view sv;
  sv.count = count;
  sv.data = data;
  return sv;
}

// <@
// @name str_view_trim_left
// @kind function
// @desc Returns a copy of the string view with leading whitespace removed.
// @param sv The string view to trim.
// @return The trimmed string view.
str_view str_view_trim_left(str_view sv) {
// @>
  size_t i = 0;
  while (i < sv.count && isspace(sv.data[i])) {
    i += 1;
  }

  return str_view_from_parts(sv.data + i, sv.count - i);
}

// <@
// @name str_view_trim_right
// @kind function
// @desc Returns a copy of the string view with trailing whitespace removed.
// @param sv The string view to trim.
// @return The trimmed string view.
str_view str_view_trim_right(str_view sv) {
// @>
  size_t i = 0;
  while (i < sv.count && isspace(sv.data[sv.count - 1 - i])) {
    i += 1;
  }

  return str_view_from_parts(sv.data, sv.count - i);
}

// <@
// @name str_view_trim
// @kind function
// @desc Returns a copy of the string view with leading and trailing whitespace removed.
// @param sv The string view to trim.
// @return The trimmed string view.
str_view str_view_trim(str_view sv) {
// @>
  return str_view_trim_right(str_view_trim_left(sv));
}

// <@
// @name str_view_from_cstr
// @kind function
// @desc Creates a str_view from a null-terminated C string.
// @param cstr The null-terminated string.
// @return A str_view referencing the string.
str_view str_view_from_cstr(const char *cstr) {
// @>
  return str_view_from_parts(cstr, strlen(cstr));
}


// <@
// @name str_view_eq
// @kind function
// @desc Compares two string views for equality.
// @param a The first string view.
// @param b The second string view.
// @return true if both string views contain the same bytes, false otherwise.
bool str_view_eq(str_view a, str_view b) {
// @>
  if (a.count != b.count) {
    return false;
  } else {
    return memcmp(a.data, b.data, a.count) == 0;
  }
}

// <@
// @name str_view_ends_with_cstr
// @kind function
// @desc Checks whether a string view ends with a null-terminated C string.
// @param sv The string view to check.
// @param cstr The suffix string.
// @return true if sv ends with cstr, false otherwise.
bool str_view_ends_with_cstr(str_view sv, const char *cstr) {
// @>
  return str_view_ends_with(sv, str_view_from_cstr(cstr));
}

// <@
// @name str_view_ends_with
// @kind function
// @desc Checks whether a string view ends with another string view.
// @param sv The string view to check.
// @param suffix The suffix to test.
// @return true if sv ends with suffix, false otherwise.
bool str_view_ends_with(str_view sv, str_view suffix) {
// @>
  if (sv.count >= suffix.count) {
    str_view sv_tail = {
      .count = suffix.count,
      .data = sv.data + sv.count - suffix.count,
    };
    return str_view_eq(sv_tail, suffix);
  }
  return false;
}

// <@
// @name str_view_starts_with
// @kind function
// @desc Checks whether a string view starts with another string view.
// @param sv The string view to check.
// @param prefix The prefix to test.
// @return true if sv starts with prefix, false otherwise.
bool str_view_starts_with(str_view sv, str_view expected_prefix) {
// @>
  if (expected_prefix.count <= sv.count) {
    str_view actual_prefix = str_view_from_parts(sv.data, expected_prefix.count);
    return str_view_eq(expected_prefix, actual_prefix);
  }

  return false;
}

#endif // USE_STR_VIEW_UTIL


#ifdef USE_DYN_ARR_UTIL

#ifdef USE_ALLOC_UTIL
#define _DYN_ARR_ALLOC_FIELD allocator alloc;
#else
#define _DYN_ARR_ALLOC_FIELD
#endif

typedef struct {
  void* data;
  usz count;
  usz capacity;

#ifdef USE_ALLOC_UTIL
  allocator alloc;
#endif
} _dyn_arr_base;

// <@
// @name dyn_arr
// @kind macro
// @desc Declares a dynamic array type for a given element type.
// @param T The element type.
// @example dyn_arr(int) numbers = {0};
// @>
#define dyn_arr(T) struct { \
  T* data;                  \
  usz count;                \
  usz capacity;             \
  _DYN_ARR_ALLOC_FIELD      \
}

#ifdef USE_ALLOC_UTIL

void _dyn_arr_ensure_allocator(
  _dyn_arr_base* arr
) {
  if (arr->alloc.alloc == nil) {
    arr->alloc =
      libc_allocator();
  }
}

#endif

void* _dyn_arr_resize(
  _dyn_arr_base* arr,
  usz elem_size,
  usz new_capacity
) {
#ifdef USE_ALLOC_UTIL

  _dyn_arr_ensure_allocator(
    arr
  );

  return arr->alloc.realloc(
    arr->alloc.ctx,
    arr->data,
    new_capacity *
      elem_size
  );

#else

  return realloc(
    arr->data,
    new_capacity *
      elem_size
  );

#endif
}

bool _dyn_arr_push_impl(
  _dyn_arr_base* arr,
  void* value,
  usz elem_size
) {
  if (arr->count >= arr->capacity) {
    usz new_capacity =
      arr->capacity > 0
      ? _int_by_1_5(
          arr->capacity
        )
      : 4;

    void* new_data =
      _dyn_arr_resize(
        arr,
        elem_size,
        new_capacity
      );

    if (new_data == nil) {
      return false;
    }

    arr->data = new_data;
    arr->capacity =
      new_capacity;
  }

  memcpy(
    (u8*)arr->data +
    arr->count *
      elem_size,
    value,
    elem_size
  );

  arr->count += 1;

  return true;
}

void _dyn_arr_free(
  _dyn_arr_base* arr
) {
#ifdef USE_ALLOC_UTIL

  _dyn_arr_ensure_allocator(
    arr
  );

  if (arr->data != nil) {
    arr->alloc.free(
      arr->alloc.ctx,
      arr->data
    );
  }

#else

  free(arr->data);

#endif

  arr->data = nil;
  arr->count = 0;
  arr->capacity = 0;
}

// <@
// @name da_push
// @kind macro
// @desc Appends a value to the dynamic array, resizing if necessary.
// @param arr Pointer to the dynamic array.
// @param value The value to append.
// @return true on success, false on allocation failure.
// @>
#define da_push(arr, value)              \
  ({                                     \
    typeof(*(arr)->data) _tmp = (value); \
    _dyn_arr_push_impl(                  \
      (_dyn_arr_base*)(arr),             \
      &_tmp,                             \
      sizeof(_tmp)                       \
    );                                   \
  })

// <@
// @name da_at
// @kind macro
// @desc Returns the element at the given index.
// No bounds checking is performed.
// @param arr Pointer to the dynamic array.
// @param index The element index.
// @>
#define da_at(arr, index) \
  ((arr)->data[(index)])

// <@
// @name da_last
// @kind macro
// @desc Returns the last element of the dynamic array.
// The array must not be empty.
// @param arr Pointer to the dynamic array.
// @>
#define da_last(arr) \
  ((arr)->data[      \
    (arr)->count - 1 \
  ])

// <@
// @name da_free
// @kind macro
// @desc Frees the memory owned by the dynamic array and resets it to an empty state.
// @param arr Pointer to the dynamic array.
// @>
#define da_free(arr)       \
  _dyn_arr_free(           \
    (_dyn_arr_base*)(arr)  \
  )

#endif // USE_DYN_ARR_UTIL



#ifdef USE_STR_BUILDER_UTIL

#include <string.h>

// <@
// @name str_builder
// @kind type
// @desc A dynamically growing string builder for constructing strings efficiently.
// The buffer is always null-terminated.
// @field data Pointer to the character buffer.
// @field count The number of bytes currently used, excluding the null terminator.
// @field capacity The total capacity of the buffer in bytes.
typedef struct {
  char* data;
  usz count;
  usz capacity;

#ifdef USE_ALLOC_UTIL
  allocator alloc;
#endif
} str_builder;
// @>

// <@
// @name sbpfmt
// @kind macro
// @desc printf format string helper for printing str_builder contents.
// @example printf(sbpfmt, sbpfarg(sb));
// @see_also sbpfarg
// @>
#define sbpfmt "%.*s"

// <@
// @name sbpfarg
// @kind macro
// @desc Expands a str_builder into printf arguments compatible with sbpfmt.
// @param sb The string builder to print.
// @see_also sbpfmt
// @>
#define sbpfarg(sb) \
  (int)(sb).count,  \
  (sb).data

#ifdef USE_ALLOC_UTIL

static void _str_builder_ensure_allocator(
  str_builder* sb
) {
  if (sb->alloc.alloc == nil) {
    sb->alloc =
      libc_allocator();
  }
}

#endif

// <@
// @name str_builder_reserve
// @kind function
// @desc Ensures that the string builder has enough capacity for additional bytes.
// @param sb The string builder.
// @param additional The number of additional bytes required.
// @return true on success, false on allocation failure.
bool str_builder_reserve(
  str_builder* sb,
  usz additional
) {
// @>
  usz required =
    sb->count +
    additional +
    1;

  if (
    required <=
    sb->capacity
  ) {
    return true;
  }

  usz new_capacity =
    sb->capacity > 0
    ? sb->capacity
    : 64;

  while (
    new_capacity <
    required
  ) {
    usz next =
      _int_by_1_5(
        new_capacity
      );

    if (
      next <=
      new_capacity
    ) {
      return false;
    }

    new_capacity = next;
  }

#ifdef USE_ALLOC_UTIL

  _str_builder_ensure_allocator(
    sb
  );

  char* new_data =
    sb->alloc.realloc(
      sb->alloc.ctx,
      sb->data,
      new_capacity
    );

#else

  char* new_data =
    realloc(
      sb->data,
      new_capacity
    );

#endif

  if (new_data == nil) {
    return false;
  }

  sb->data = new_data;
  sb->capacity =
    new_capacity;

  return true;
}

// <@
// @name str_builder_append_bytes
// @kind function
// @desc Appends raw bytes to the string builder.
// @param sb The string builder.
// @param data Pointer to the bytes to append.
// @param size Number of bytes to append.
// @return true on success, false on allocation failure.
bool str_builder_append_bytes(
  str_builder* sb,
  const void* data,
  usz size
) {
// @>
  if (
    !str_builder_reserve(
      sb,
      size
    )
  ) {
    return false;
  }

  memcpy(
    sb->data +
      sb->count,
    data,
    size
  );

  sb->count += size;

  sb->data[
    sb->count
  ] = '\0';

  return true;
}

// <@
// @name str_builder_append_cstr
// @kind function
// @desc Appends a null-terminated C string to the string builder.
// @param sb The string builder.
// @param cstr The string to append.
// @return true on success, false on allocation failure.
bool str_builder_append_cstr(
  str_builder* sb,
  const char* cstr
) {
// @>
  return str_builder_append_bytes(
    sb,
    cstr,
    strlen(cstr)
  );
}

// <@
// @name str_builder_append_sb
// @kind function
// @desc Appends the contents of another string builder.
// @param sb The destination string builder.
// @param other The source string builder.
// @return true on success, false on allocation failure.
bool str_builder_append_sb(
  str_builder* sb,
  const str_builder* other
) {
// @>
  return str_builder_append_bytes(
    sb,
    other->data,
    other->count
  );
}

bool _str_builder_append_sb_value(
  str_builder* sb,
  str_builder other
) {
  return str_builder_append_sb(
    sb,
    &other
  );
}

#ifdef USE_STR_VIEW_UTIL

// <@
// @name str_builder_append_sv
// @kind function
// @desc Appends a string view to the string builder.
// @param sb The destination string builder.
// @param sv The string view to append.
// @return true on success, false on allocation failure.
bool str_builder_append_sv(
  str_builder* sb,
  str_view sv
) {
// @>
  return str_builder_append_bytes(
    sb,
    sv.data,
    sv.count
  );
}

// <@
// @name str_builder_view
// @kind function
// @desc Returns a string view referencing the contents of the string builder.
// @param sb The string builder.
// @return A str_view referencing the builder contents.
str_view str_builder_view(
  const str_builder* sb
) {
// @>
  return str_view_from_parts(
    sb->data
      ? sb->data
      : "",
    sb->count
  );
}

#endif // USE_STR_VIEW_UTIL

#ifdef USE_STR_VIEW_UTIL

#define _STR_BUILDER_APPEND_SV_TYPES \
  , str_view: str_builder_append_sv

#else

#define _STR_BUILDER_APPEND_SV_TYPES

#endif

// <@
// @name str_builder_append
// @kind macro
// @desc Generic append macro for appending C-strings, string builders and string views.
// Supported types depend on enabled utilities.
// @param sb The destination string builder.
// @param data The value to append.
// @>
#define str_builder_append(sb, data)       \
  _Generic((data),                         \
    char*:                                 \
      str_builder_append_cstr,             \
    const char*:                           \
      str_builder_append_cstr,             \
    str_builder:                           \
      _str_builder_append_sb_value,        \
    str_builder*:                          \
      str_builder_append_sb,               \
    const str_builder*:                    \
      str_builder_append_sb                \
    _STR_BUILDER_APPEND_SV_TYPES           \
  )(sb, data)

// <@
// @name str_builder_clear
// @kind function
// @desc Clears the contents of the string builder without freeing its memory.
// @param sb The string builder to clear.
void str_builder_clear(
  str_builder* sb
) {
// @>
  sb->count = 0;

  if (sb->data != nil) {
    sb->data[0] = '\0';
  }
}

// <@
// @name str_builder_free
// @kind function
// @desc Frees the memory owned by the string builder and resets it to an empty state.
// @param sb The string builder to free.
void str_builder_free(
  str_builder* sb
) {
// @>
#ifdef USE_ALLOC_UTIL

  _str_builder_ensure_allocator(
    sb
  );

  if (sb->data != nil) {
    sb->alloc.free(
      sb->alloc.ctx,
      sb->data
    );
  }

#else

  free(sb->data);

#endif

  sb->data = nil;
  sb->count = 0;
  sb->capacity = 0;
}

#endif // USE_STR_BUILDER_UTIL



#ifdef USE_FILE_UTIL

#include <stdio.h>

#ifdef USE_STR_BUILDER_UTIL

// <@
// @name read_entire_file
// @kind function
// @desc Reads the entire contents of a file into a string builder.
// Existing contents of the string builder are cleared.
// @param path Path to the file.
// @param sb Destination string builder.
// @return true on success, false on failure.
bool read_entire_file(
  const char* path,
  str_builder* sb
) {
// @>
  FILE* f = fopen(path, "rb");

  if (f == nil) {
    return false;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }

  long size = ftell(f);

  if (size < 0) {
    fclose(f);
    return false;
  }

  rewind(f);

  str_builder_clear(sb);

  if (
    !str_builder_reserve(
      sb,
      (usz)size
    )
  ) {
    fclose(f);
    return false;
  }

  usz read =
    fread(
      sb->data,
      1,
      (usz)size,
      f
    );

  fclose(f);

  if (read != (usz)size) {
    return false;
  }

  sb->count = read;
  sb->data[sb->count] = '\0';

  return true;
}

#endif // USE_STR_BUILDER_UTIL

// <@
// @name write_entire_file_cstr
// @kind function
// @desc Writes a null-terminated C string to a file, replacing its contents.
// @param path Path to the file.
// @param data The string to write.
// @return true on success, false on failure.
bool write_entire_file_cstr(
  const char* path,
  const char* data
) {
// @>
  FILE* f = fopen(path, "wb");

  if (f == nil) {
    return false;
  }

  usz size = strlen(data);

  usz written =
    fwrite(
      data,
      1,
      size,
      f
    );

  fclose(f);

  return written == size;
}

#ifdef USE_STR_VIEW_UTIL

// <@
// @name write_entire_file_sv
// @kind function
// @desc Writes a string view to a file, replacing its contents.
// @param path Path to the file.
// @param sv The string view to write.
// @return true on success, false on failure.
bool write_entire_file_sv(
  const char* path,
  str_view sv
) {
// @>
  FILE* f = fopen(path, "wb");

  if (f == nil) {
    return false;
  }

  usz written =
    fwrite(
      sv.data,
      1,
      sv.count,
      f
    );

  fclose(f);

  return written == sv.count;
}


// <@
// @name write_entire_file_sv_ptr
// @kind function
// @desc Writes a string view pointed to by sv to a file.
// @param path Path to the file.
// @param sv Pointer to the string view to write.
// @return true on success, false on failure.
bool write_entire_file_sv_ptr(
  const char* path,
  str_view* sv
) {
// @>
  if (sv == nil) {
    return false;
  }

  return write_entire_file_sv(
    path,
    *sv
  );
}

#endif // USE_STR_VIEW_UTIL

#ifdef USE_STR_BUILDER_UTIL

// <@
// @name write_entire_file_sb
// @kind function
// @desc Writes the contents of a string builder to a file.
// @param path Path to the file.
// @param sb The string builder to write.
// @return true on success, false on failure.
bool write_entire_file_sb(
  const char* path,
  str_builder sb
) {
// @>
  FILE* f = fopen(path, "wb");

  if (f == nil) {
    return false;
  }

  usz written =
    fwrite(
      sb.data,
      1,
      sb.count,
      f
    );

  fclose(f);

  return written == sb.count;
}

// <@
// @name write_entire_file_sb_ptr
// @kind function
// @desc Writes the contents of a string builder pointed to by sb to a file.
// @param path Path to the file.
// @param sb Pointer to the string builder to write.
// @return true on success, false on failure.
bool write_entire_file_sb_ptr(
  const char* path,
  str_builder* sb
) {
// @>
  if (sb == nil) {
    return false;
  }

  return write_entire_file_sb(
    path,
    *sb
  );
}

#endif // USE_STR_BUILDER_UTIL

#ifdef USE_STR_VIEW_UTIL
#define _WRITE_FILE_SV_TYPES \
  , str_view: write_entire_file_sv \
  , str_view*: write_entire_file_sv_ptr \
  , const str_view*: write_entire_file_sv_ptr
#else
#define _WRITE_FILE_SV_TYPES
#endif

#ifdef USE_STR_BUILDER_UTIL
#define _WRITE_FILE_SB_TYPES \
  , str_builder: write_entire_file_sb \
  , str_builder*: write_entire_file_sb_ptr \
  , const str_builder*: write_entire_file_sb_ptr
#else
#define _WRITE_FILE_SB_TYPES
#endif

// <@
// @name write_entire_file
// @kind macro
// @desc Generic file writing macro supporting C strings, string views and string builders.
// Supported types depend on enabled utilities.
// @param path Path to the file.
// @param data The data to write.
// @>
#define write_entire_file(path, data)   \
  _Generic((data),                      \
    char*: write_entire_file_cstr,      \
    const char*: write_entire_file_cstr \
    _WRITE_FILE_SV_TYPES                 \
    _WRITE_FILE_SB_TYPES                 \
  )(path, data)

#endif // USE_FILE_UTIL

#endif // _UTILS_C
