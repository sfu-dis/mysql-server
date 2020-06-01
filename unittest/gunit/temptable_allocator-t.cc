/* Copyright (c) 2016, 2020, Oracle and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 2.0, as published by the
Free Software Foundation.

This program is also distributed with certain software (including but not
limited to OpenSSL) that is licensed under separate terms, as designated in a
particular file or component or in included license documentation. The authors
of MySQL hereby grant you an additional permission to link the program and
your derivative works with the separately licensed software that they have
included with MySQL.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License, version 2.0,
for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

// First include (the generated) my_config.h, to get correct platform defines.
#include "my_config.h"

#include <gtest/gtest.h>
#include <array>

#include "storage/temptable/include/temptable/allocator.h"
#include "storage/temptable/include/temptable/block.h"
#include "storage/temptable/include/temptable/constants.h"

namespace temptable_allocator_unittest {

/** GoogleTest macros for testing exceptions (EXPECT_THROW, EXPECT_NO_THROW,
 * EXPECT_ANY_THROW, etc.) do not provide direct means to inspect the value of
 * exception that has been thrown.
 *
 * Indirectly, it is possible to inspect the value but the test-body becomes
 * unnecessarily verbose, longer and more prone to introducing errors. E.g.
 *
 *    try {
 *        Foo foo;
 *        foo.bar();
 *        FAIL() << "We must not reach here. Expected std::out_of_range";
 *    }
 *    catch (const std::out_of_range& ex) {
 *        EXPECT_EQ(ex.what(), std::string("Out of range"));
 *    }
 *    catch (...) {
 *        FAIL() << "We must not reach here. Expected std::out_of_range";
 *    }
 *
 * Following set of macros extend GoogleTest set of macros and enables
 * us to express our intent in much more cleaner way. E.g.
 *
 *   Foo foo;
 *   EXPECT_THROW_WITH_VALUE_STR(foo.bar(), std::out_of_range, "Out of range");
 *
 * EXPECT_THROW_WITH_VALUE macro is for catching exceptions which are not
 * derived from std::exception and whose value can be inspected by a mere usage
 * of operator==.
 *
 * EXPECT_THROW_WITH_VALUE_STR macro is for catching exceptions which provide
 * ::what() interface to inspect the value, such as all exceptions which
 * are derived from std::exception.
 * */
#define EXPECT_THROW_WITH_VALUE(stmt, etype, value) \
  EXPECT_THROW(                                     \
      try { stmt; } catch (const etype &ex) {       \
        EXPECT_EQ(ex, value);                       \
        throw;                                      \
      },                                            \
      etype)
#define EXPECT_THROW_WITH_VALUE_STR(stmt, etype, str) \
  EXPECT_THROW(                                       \
      try { stmt; } catch (const etype &ex) {         \
        EXPECT_EQ(std::string(ex.what()), str);       \
        throw;                                        \
      },                                              \
      etype)

// A "probe" which gains us read-only access to temptable::MemoryMonitor.
// Necessary for implementing certain unit-tests.
struct MemoryMonitorReadOnlyProbe : public temptable::MemoryMonitor {
  static size_t ram_consumption() {
    return temptable::MemoryMonitor::RAM::consumption();
  }
  static size_t ram_threshold() {
    return temptable::MemoryMonitor::RAM::threshold();
  }
  static bool mmap_enabled() { return temptable_use_mmap; }
  static size_t mmap_consumption() {
    return temptable::MemoryMonitor::MMAP::consumption();
  }
  static size_t mmap_threshold() {
    return temptable::MemoryMonitor::MMAP::threshold();
  }
};

// A "probe" which enables us to hijack the temptable::MemoryMonitor.
// Necessary for implementing certain unit-tests.
struct MemoryMonitorHijackProbe : public MemoryMonitorReadOnlyProbe {
  static size_t ram_consumption_reset() {
    auto current_consumption = temptable::MemoryMonitor::RAM::consumption();
    return temptable::MemoryMonitor::RAM::decrease(current_consumption);
  }
  static size_t mmap_consumption_reset() {
    auto current_consumption = temptable::MemoryMonitor::MMAP::consumption();
    return temptable::MemoryMonitor::MMAP::decrease(current_consumption);
  }
  static void mmap_enable() { temptable_use_mmap = true; }
  static void mmap_disable() { temptable_use_mmap = false; }
  static void max_ram_set(size_t new_max_ram) {
    temptable_max_ram = new_max_ram;
  }
  static void max_mmap_set(size_t new_max_mmap) {
    temptable_max_mmap = new_max_mmap;
  }
};

class TempTableAllocator : public ::testing::Test {
 protected:
  void SetUp() override {
    // Store the default thresholds of RAM and MMAP so we can restore them to
    // the original values prior to starting a new test
    m_default_ram_threshold = MemoryMonitorHijackProbe::ram_threshold();
    m_default_mmap_threshold = MemoryMonitorHijackProbe::mmap_threshold();

    // Reset the RAM and MMAP consumption counters to zero
    EXPECT_EQ(MemoryMonitorHijackProbe::ram_consumption_reset(), 0);
    EXPECT_EQ(MemoryMonitorHijackProbe::mmap_consumption_reset(), 0);

    // Enable MMAP by default
    MemoryMonitorHijackProbe::mmap_enable();
  }

  void TearDown() override {
    // Restore the original RAM and MMAP thresholds
    MemoryMonitorHijackProbe::max_ram_set(m_default_ram_threshold);
    MemoryMonitorHijackProbe::max_mmap_set(m_default_mmap_threshold);
  }

  size_t m_default_ram_threshold;
  size_t m_default_mmap_threshold;
};

TEST_F(TempTableAllocator, basic) {
  temptable::Block shared_block;
  EXPECT_TRUE(shared_block.is_empty());
  temptable::Allocator<uint8_t> allocator(&shared_block);

  constexpr size_t n_allocate = 128;
  std::array<uint8_t *, n_allocate> a;
  constexpr size_t n_elements = 16;

  for (size_t i = 0; i < n_allocate; ++i) {
    a[i] = allocator.allocate(n_elements);

    for (size_t j = 0; j < n_elements; ++j) {
      a[i][j] = 0xB;
    }
  }

  EXPECT_FALSE(shared_block.is_empty());

  for (size_t i = 0; i < n_allocate; ++i) {
    allocator.deallocate(a[i], n_elements);
  }

  // Physically deallocate the shared-block (allocator keeps it alive
  // intentionally)
  EXPECT_FALSE(shared_block.is_empty());
  shared_block.destroy();
  EXPECT_TRUE(shared_block.is_empty());
}

TEST_F(TempTableAllocator,
       allocation_successful_when_shared_block_is_not_available) {
  // No shared-block is available to be used by the allocator
  temptable::Allocator<uint8_t> allocator(nullptr);
  uint32_t n_elements = 16;

  // Trigger the allocation
  uint8_t *chunk = nullptr;
  EXPECT_NO_THROW(chunk = allocator.allocate(n_elements));
  EXPECT_NE(chunk, nullptr);

  // Clean-up
  allocator.deallocate(chunk, n_elements);
}

TEST_F(TempTableAllocator, shared_block_is_kept_after_last_deallocation) {
  temptable::Block shared_block;
  EXPECT_TRUE(shared_block.is_empty());
  temptable::Allocator<uint8_t> allocator(&shared_block);

  uint8_t *ptr = allocator.allocate(16);
  EXPECT_FALSE(shared_block.is_empty());

  allocator.deallocate(ptr, 16);

  // Physically deallocate the shared-block (allocator keeps it alive
  // intentionally)
  EXPECT_FALSE(shared_block.is_empty());
  shared_block.destroy();
  EXPECT_TRUE(shared_block.is_empty());
}

TEST_F(TempTableAllocator, rightmost_chunk_deallocated_reused_for_allocation) {
  temptable::Block shared_block;
  EXPECT_TRUE(shared_block.is_empty());
  temptable::Allocator<uint8_t> allocator(&shared_block);

  // Allocate first Chunk which is less than the 1MB
  size_t first_chunk_size = 512 * 1024;
  uint8_t *first_chunk = allocator.allocate(first_chunk_size);

  // Calculate and allocate second chunk in such a way that
  // it lies within the block and fills it
  size_t first_chunk_actual_size =
      temptable::Chunk::size_hint(first_chunk_size);
  size_t space_left_in_block =
      shared_block.size() -
      temptable::Block::size_hint(first_chunk_actual_size);
  size_t second_chunk_size =
      space_left_in_block - (first_chunk_actual_size - first_chunk_size);
  uint8_t *second_chunk = allocator.allocate(second_chunk_size);

  // Make sure that pointers (Chunk's) are from same blocks
  EXPECT_EQ(temptable::Block(temptable::Chunk(first_chunk)),
            temptable::Block(temptable::Chunk(second_chunk)));

  EXPECT_FALSE(shared_block.can_accommodate(1));

  // Deallocate Second Chunk
  allocator.deallocate(second_chunk, second_chunk_size);

  // Allocate Second Chunk again
  second_chunk = allocator.allocate(second_chunk_size);

  // Make sure that pointers (Chunk's) are from same blocks
  EXPECT_EQ(temptable::Block(temptable::Chunk(first_chunk)),
            temptable::Block(temptable::Chunk(second_chunk)));

  // Deallocate Second Chunk
  allocator.deallocate(second_chunk, second_chunk_size);

  // Deallocate First Chunk
  allocator.deallocate(first_chunk, first_chunk_size);

  // Physically deallocate the shared-block (allocator keeps it alive
  // intentionally)
  EXPECT_FALSE(shared_block.is_empty());
  shared_block.destroy();
  EXPECT_TRUE(shared_block.is_empty());
}

TEST_F(TempTableAllocator, ram_consumption_is_not_monitored_for_shared_blocks) {
  temptable::Block shared_block;
  EXPECT_TRUE(shared_block.is_empty());
  temptable::Allocator<uint8_t> allocator(&shared_block);

  // RAM consumption is 0 at the start
  EXPECT_EQ(MemoryMonitorReadOnlyProbe::ram_consumption(), 0);

  // First allocation is fed from shared-block
  size_t shared_block_n_elements = 1024 * 1024;
  uint8_t *shared_block_ptr = allocator.allocate(shared_block_n_elements);
  EXPECT_FALSE(shared_block.is_empty());

  // RAM consumption is still 0
  EXPECT_EQ(MemoryMonitorReadOnlyProbe::ram_consumption(), 0);

  // Deallocate the shared-block
  allocator.deallocate(shared_block_ptr, shared_block_n_elements);

  // Physically deallocate the shared-block (allocator keeps it alive
  // intentionally)
  EXPECT_FALSE(shared_block.is_empty());
  shared_block.destroy();
  EXPECT_TRUE(shared_block.is_empty());
}

TEST_F(TempTableAllocator,
       ram_consumption_drops_to_zero_when_non_shared_block_is_destroyed) {
  temptable::Block shared_block;
  EXPECT_TRUE(shared_block.is_empty());
  temptable::Allocator<uint8_t> allocator(&shared_block);

  // RAM consumption is 0 at the start
  EXPECT_EQ(MemoryMonitorReadOnlyProbe::ram_consumption(), 0);

  // Make sure we fill up the shared_block first
  // nr of elements must be >= 1MiB in size
  size_t shared_block_n_elements = 1024 * 1024 + 256;
  uint8_t *shared_block_ptr = allocator.allocate(shared_block_n_elements);
  EXPECT_FALSE(shared_block.is_empty());

  // Not even 1-byte should be able to fit anymore
  EXPECT_FALSE(shared_block.can_accommodate(1));

  // Now our next allocation should result in new block allocation ...
  size_t non_shared_block_n_elements = 2 * 1024;
  uint8_t *non_shared_block_ptr =
      allocator.allocate(non_shared_block_n_elements);

  // Make sure that pointers (Chunk's) are from different blocks
  EXPECT_NE(temptable::Block(temptable::Chunk(non_shared_block_ptr)),
            temptable::Block(temptable::Chunk(shared_block_ptr)));

  // RAM consumption should be greater or equal than
  // non_shared_block_n_elements bytes at this point
  EXPECT_GE(MemoryMonitorReadOnlyProbe::ram_consumption(),
            non_shared_block_n_elements);

  // Deallocate the non-shared block
  allocator.deallocate(non_shared_block_ptr, non_shared_block_n_elements);

  // RAM consumption must drop to 0
  EXPECT_EQ(MemoryMonitorReadOnlyProbe::ram_consumption(), 0);

  // Deallocate the shared-block
  allocator.deallocate(shared_block_ptr, shared_block_n_elements);

  // Physically deallocate the shared-block (allocator keeps it alive
  // intentionally)
  EXPECT_FALSE(shared_block.is_empty());
  shared_block.destroy();
  EXPECT_TRUE(shared_block.is_empty());
}

TEST_F(TempTableAllocator, zero_size_allocation_returns_nullptr) {
  temptable::Allocator<uint8_t> allocator(nullptr);
  EXPECT_EQ(nullptr, allocator.allocate(0));
}

TEST_F(TempTableAllocator, block_size_cap) {
  temptable::Block shared_block;
  EXPECT_TRUE(shared_block.is_empty());
  temptable::Allocator<uint8_t> allocator(&shared_block);

  using namespace temptable;

  constexpr size_t alloc_size = 1_MiB;
  constexpr size_t n_allocate = ALLOCATOR_MAX_BLOCK_BYTES / alloc_size + 10;
  std::array<uint8_t *, n_allocate> a;

  for (size_t i = 0; i < n_allocate; ++i) {
    a[i] = allocator.allocate(alloc_size);
  }

  EXPECT_FALSE(shared_block.is_empty());

  for (size_t i = 0; i < n_allocate; ++i) {
    allocator.deallocate(a[i], alloc_size);
  }

  // Physically deallocate the shared-block (allocator keeps it alive
  // intentionally)
  EXPECT_FALSE(shared_block.is_empty());
  shared_block.destroy();
  EXPECT_TRUE(shared_block.is_empty());
}

// Create some aliases to make our life easier when generating the test-cases
// down below.
using max_ram = decltype(temptable_max_ram);
using max_mmap = decltype(temptable_max_mmap);
using use_mmap = decltype(temptable_use_mmap);
using n_elements = uint32_t;
using is_ram_expected_to_be_increased = bool;
using is_mmap_expected_to_be_increased = bool;

// Parametrized test for testing successful allocation patterns
class AllocatesSuccessfully
    : public TempTableAllocator,
      public ::testing::WithParamInterface<std::tuple<
          max_ram, max_mmap, use_mmap, n_elements,
          is_ram_expected_to_be_increased, is_mmap_expected_to_be_increased>> {
};

// Parametrized test for testing allocation patterns which should yield
// RecordFileFull exception
class ThrowsRecordFileFull
    : public TempTableAllocator,
      public ::testing::WithParamInterface<
          std::tuple<max_ram, max_mmap, use_mmap, n_elements>> {};

// Implementation of parametrized test-cases which tests successful allocation
// patterns
TEST_P(AllocatesSuccessfully,
       for_various_allocation_patterns_and_configurations) {
  auto max_ram = std::get<0>(GetParam());
  auto max_mmap = std::get<1>(GetParam());
  auto mmap_enabled = std::get<2>(GetParam());
  auto n_elements = std::get<3>(GetParam());
  auto ram_expected_to_be_increased = std::get<4>(GetParam());
  auto mmap_expected_to_be_increased = std::get<5>(GetParam());

  MemoryMonitorHijackProbe::max_ram_set(max_ram);
  MemoryMonitorHijackProbe::max_mmap_set(max_mmap);
  mmap_enabled ? MemoryMonitorHijackProbe::mmap_enable()
               : MemoryMonitorHijackProbe::mmap_disable();

  // Trigger the allocation
  temptable::Allocator<uint8_t> allocator(nullptr);
  uint8_t *chunk = nullptr;
  EXPECT_NO_THROW(chunk = allocator.allocate(n_elements));
  EXPECT_NE(chunk, nullptr);

  // After successfull allocation, and depending on the use-case, RAM and MMAP
  // consumption should increase or stay at the same level accordingly
  if (ram_expected_to_be_increased) {
    EXPECT_GE(MemoryMonitorReadOnlyProbe::ram_consumption(),
              n_elements * sizeof(uint8_t));
  } else {
    EXPECT_EQ(MemoryMonitorReadOnlyProbe::ram_consumption(), 0);
  }
  if (mmap_expected_to_be_increased) {
    EXPECT_GE(MemoryMonitorReadOnlyProbe::mmap_consumption(),
              n_elements * sizeof(uint8_t));
  } else {
    EXPECT_EQ(MemoryMonitorReadOnlyProbe::mmap_consumption(), 0);
  }

  // Clean-up
  allocator.deallocate(chunk, n_elements);
}

// Implementation of parametrized test-cases which tests allocation patterns
// yielding RecordFileFull exception
TEST_P(ThrowsRecordFileFull,
       for_various_allocation_patterns_and_configurations) {
  auto max_ram = std::get<0>(GetParam());
  auto max_mmap = std::get<1>(GetParam());
  auto mmap_enabled = std::get<2>(GetParam());
  auto n_elements = std::get<3>(GetParam());

  MemoryMonitorHijackProbe::max_ram_set(max_ram);
  MemoryMonitorHijackProbe::max_mmap_set(max_mmap);
  mmap_enabled ? MemoryMonitorHijackProbe::mmap_enable()
               : MemoryMonitorHijackProbe::mmap_disable();

  // Trigger the allocation
  temptable::Allocator<uint8_t> allocator(nullptr);
  uint8_t *chunk = nullptr;
  EXPECT_THROW_WITH_VALUE(chunk = allocator.allocate(n_elements),
                          temptable::Result,
                          temptable::Result::RECORD_FILE_FULL);
  EXPECT_EQ(chunk, nullptr);
  // After allocation failure, RAM consumption must remain intact (zero)
  EXPECT_EQ(MemoryMonitorReadOnlyProbe::ram_consumption(), 0);
  // Ditto for MMAP
  EXPECT_EQ(MemoryMonitorReadOnlyProbe::mmap_consumption(), 0);
}

// Needed for making it possible to use user-defined literals (e.g. 1_MiB) when
// instantiating (generating) test-cases below
using temptable::operator"" _KiB;
using temptable::operator"" _MiB;

// Generate tests for all of the test-case scenarios which should yield
// RecordFileFull exception
INSTANTIATE_TEST_SUITE_P(
    TempTableAllocator, ThrowsRecordFileFull,
    ::testing::Values(
        // ram threshold reached, mmap threshold not reached, mmap disabled
        std::make_tuple(1_MiB, 2_MiB, false, 1_MiB + 1),
        // ram threshold reached, mmap threshold reached, mmap disabled
        std::make_tuple(1_MiB, 1_MiB, false, 2_MiB),
        // ram threshold reached, mmap threshold reached, mmap enabled
        std::make_tuple(1_MiB, 1_MiB, true, 2_MiB),
        // ram threshold reached, mmap threshold reached (but set to 0), mmap
        // disabled
        std::make_tuple(1_MiB, 0_MiB, false, 2_MiB),
        // ram threshold reached, mmap threshold reached (but set to 0), mmap
        // enabled
        std::make_tuple(1_MiB, 0_MiB, true, 2_MiB)));

// Generate tests for all of the test-case scenarios which should result with a
// successful allocation
INSTANTIATE_TEST_SUITE_P(
    TempTableAllocator, AllocatesSuccessfully,
    ::testing::Values(
        // ram threshold not reached, mmap threshold not reached (but set to 0),
        // mmap disabled
        std::make_tuple(1_MiB, 0_MiB, false, 2_KiB, true, false),
        // ram threshold not reached, mmap threshold not reached (but set to 0),
        // mmap enabled
        std::make_tuple(1_MiB, 0_MiB, true, 2_KiB, true, false),
        // ram threshold not reached, mmap threshold not reached, mmap disabled
        std::make_tuple(1_MiB, 1_MiB, true, 2_KiB, true, false),
        // ram threshold not reached, mmap threshold not reached, mmap enabled
        std::make_tuple(1_MiB, 1_MiB, true, 2_KiB, true, false),
        // ram threshold reached, mmap threshold not reached, mmap enabled
        std::make_tuple(1_MiB, 4_MiB, true, 2_MiB, false, true)));

} /* namespace temptable_allocator_unittest */
