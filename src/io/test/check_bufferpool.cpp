/**
 * @file		check_bufferpool.cpp
 * @ingroup
 * @author	tpan
 * @brief
 * @details
 *
 * Copyright (c) 2014 Georgia Institute of Technology.  All Rights Reserved.
 *
 * TODO add License
 */

#include <unistd.h>  // for usleep

#include "io/buffer_pool.hpp"
#include "omp.h"
#include <cassert>
#include <chrono>
#include <vector>
#include <cstdlib>   // for rand



template<typename PoolType>
void testPool(PoolType && pool, const std::string &name, int pool_threads, int buffer_threads) {

  using BufferType = typename PoolType::BufferType;


  printf("TESTING %s %s: pool threads %d, buffer threads %d\n", name.c_str(), (pool.isUnlimited() ? "FIXED" : "GROW"),  pool_threads, buffer_threads);

  printf("TEST acquire\n");
  int expected;
  int i = 0;
  int count = 0;
  int mx = pool.isUnlimited() ? 100 : pool.getCapacity();
#pragma omp parallel for num_threads(pool_threads) default(none) private(i) shared(pool, mx) reduction(+ : count)
  for (i = 0; i < mx; ++i) {
	  auto ptr = std::move(pool.tryAcquireBuffer());
    if (! ptr) {
      ++count;
    }
  }
  expected = 0;
  if (count != expected) printf("ERROR: number of failed attempt to acquire buffer should be %d, actual %d.  pool capacity %lu, remaining: %lu \n", expected, count, pool.getCapacity(), pool.getAvailableCount());
  pool.reset();

  printf("TEST acquire with growth\n");
  i = 0;
  count = 0;
  mx = pool.isUnlimited() ? 100 : pool.getCapacity();
#pragma omp parallel for num_threads(pool_threads) default(none) private(i) shared(pool, mx) reduction(+ : count)
  for (i = 0; i <= mx; ++i) {  // <= so we get 1 extra
		auto ptr = std::move(pool.tryAcquireBuffer());
	    if (! ptr) {
	      ++count;
	    }
  }
  expected = pool.isUnlimited() ? 0 : 1;
  if (count != expected) printf("ERROR: number of failed attempt to acquire buffer should be %d, actual %d.  pool remaining: %lu \n", expected, count, pool.getAvailableCount());

  pool.reset();

  printf("TEST release\n");
  count = 0;
  mx = pool.isUnlimited() ? 100 : pool.getCapacity();
  // first drain the pool
  for (i = 0; i < mx; ++i) {
    auto ptr = std::move(pool.tryAcquireBuffer());
  }
  // and create some dummy buffers to insert
  std::vector<typename PoolType::BufferPtrType> temp;
  for (i = 0; i < mx; ++i) {
    temp.push_back(std::move(std::unique_ptr<BufferType>(new BufferType(pool.getBufferCapacity()))));
    temp.push_back(std::move(std::unique_ptr<BufferType>(new BufferType(pool.getBufferCapacity()))));
  }
#pragma omp parallel for num_threads(pool_threads) default(none) shared(pool, mx, temp) private(i) reduction(+ : count)
  for (i = 0; i < mx * 2; ++i) {

    auto ptr = std::move(temp[i]);
    if (ptr) {
      ptr->block();
      if (! pool.releaseBuffer(std::move(ptr))) {
        ++count; // failed release
      }
    }
  }
  expected = mx;  // unlimited or not, can only push back in as much as taken out.
  if (count != expected) printf("ERROR: number of failed attempt to release buffer should be %d, actual %d. pool remaining: %lu \n", expected, count, pool.getAvailableCount());
  pool.reset();
  temp.clear();

  printf("TEST access by multiple threads, each a separate buffer.\n");


  count = 0;
  int count1 = 0;
#pragma omp parallel num_threads(pool_threads) default(none) shared(pool) reduction(+ : count, count1)
  {
    int v = omp_get_thread_num() + 5;
    auto ptr = std::move(pool.tryAcquireBuffer());
    if (! ptr->append(&v, sizeof(int))) {
      ++count;
    }

    int u = reinterpret_cast<const int*>(ptr->getData())[0];
    if (v != u) {
      ++count1;
    }

    ptr->block();
    pool.releaseBuffer(std::move(ptr));
  }
  if (count != 0) printf("ERROR: append failed\n");
  else if (count1 != 0) printf("ERROR: inserted and got back\n");
  pool.reset();

  printf("TEST access by multiple threads, all to same buffer.\n");


  auto ptr = std::move(pool.tryAcquireBuffer());
#pragma omp parallel num_threads(buffer_threads) default(none) shared(pool, ptr)
  {
    int v = 7;
    ptr->append(&v, sizeof(int));
  }

  bool same = true;
  for (int i = 0; i < buffer_threads ; ++i) {
    same &= reinterpret_cast<const int*>(ptr->getData())[i] == 7;
  }
  if (!same) printf("ERROR: inserted not same\n");
  pool.reset();


  omp_set_nested(1);

  printf("TEST all operations together\n");
#pragma omp parallel num_threads(pool_threads) default(none) shared(pool, pool_threads, buffer_threads)
  {
    // Id range is 0 to 100
    int iter;
    int j = 0;
    for (int i = 0; i < 100; ++i) {
      // acquire
      auto buf = pool.tryAcquireBuffer();
      while (!buf) {
        usleep(50);
        buf = pool.tryAcquireBuffer();
      }

      // access
      iter = rand() % 100;
#pragma omp parallel for num_threads(buffer_threads) default(none) shared(pool, buf, iter) private(j)
      for (j = 0; j < iter; ++j) {
        buf->append(&j, sizeof(int));
      }

      // random sleep
      usleep(rand() % 1000);
      if (buf->getSize() != sizeof(int) * iter)
        printf("ERROR: thread %d/%d buffer size is %lu, expected %lu\n", omp_get_thread_num(), pool_threads, buf->getSize(), sizeof(int) * iter);

      // clear buffer
      buf->block();
      //release
      pool.releaseBuffer(std::move(buf));
      //if (i % 25 == 0)
//      printf("thread %d released buffer %d\n", omp_get_thread_num(), id);

    }
  }




};


int main(int argc, char** argv) {

  // construct, acquire, access, release

  //////////////  unbounded version

  /// thread unsafe.  test in single thread way.
//  bliss::io::BufferPool<bliss::concurrent::THREAD_UNSAFE> usPool(8192);


  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_UNSAFE>(8192)), "thread unsafe pool", 1, 1);




  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 1, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 1, 2);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 1, 3);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 1, 4);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 1, 8);

  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 2, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 2, 2);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 2, 3);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 2, 4);

  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 3, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 3, 2);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8192)), "thread safe pool, thread safe buffer", 3, 3);






  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE, bliss::concurrent::THREAD_UNSAFE>(8192)), "thread safe pool, thread unsafe buffer", 1, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE, bliss::concurrent::THREAD_UNSAFE>(8192)), "thread safe pool, thread unsafe buffer", 2, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE, bliss::concurrent::THREAD_UNSAFE>(8192)), "thread safe pool, thread unsafe buffer", 3, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE, bliss::concurrent::THREAD_UNSAFE>(8192)), "thread safe pool, thread unsafe buffer", 4, 1);




  // this one is not defined because it's not logical.  not compilable.
  // bliss::io::BufferPool<bliss::concurrent::THREAD_UNSAFE, bliss::concurrent::THREAD_SAFE> tsusPool(8192, 8);




  /////////////  fixed size version.



  /// testing buffer_pool. thread unsafe


  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_UNSAFE>(8, 8192)), "thread unsafe pool", 1, 1);




  /// testing buffer_pool. thread safe

  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 1, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 1, 2);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 1, 3);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 1, 4);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 1, 8);

  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 2, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 2, 2);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 2, 3);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 2, 4);

  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 3, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 3, 2);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE>(8, 8192)), "thread safe pool, thread safe buffer", 3, 3);



  /// testing buffer_pool, thread safe pool, unsafe buffers.

  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE, bliss::concurrent::THREAD_UNSAFE>(8, 8192)), "thread safe pool, thread unsafe buffer", 1, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE, bliss::concurrent::THREAD_UNSAFE>(8, 8192)), "thread safe pool, thread unsafe buffer", 2, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE, bliss::concurrent::THREAD_UNSAFE>(8, 8192)), "thread safe pool, thread unsafe buffer", 3, 1);
  testPool(std::move(bliss::io::BufferPool<bliss::concurrent::THREAD_SAFE, bliss::concurrent::THREAD_UNSAFE>(8, 8192)), "thread safe pool, thread unsafe buffer", 4, 1);


  // this one is not defined because it's not logical.  not compilable.
  // bliss::io::BufferPool<bliss::concurrent::THREAD_UNSAFE, bliss::concurrent::THREAD_SAFE> tsusPool(8, 8192);





}
