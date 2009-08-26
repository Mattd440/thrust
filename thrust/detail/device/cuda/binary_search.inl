/*
 *  Copyright 2008-2009 NVIDIA Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */


/*! \file binary_search.inl
 *  \brief Inline file for binary_search.h
 */

#pragma once

// do not attempt to compile this file with any other compiler
#ifdef __CUDACC__

#include <algorithm>

#include <thrust/detail/raw_buffer.h>

#include <thrust/iterator/iterator_traits.h>

#include <thrust/detail/device/dereference.h>
#include <thrust/detail/type_traits.h>

namespace thrust
{

namespace detail
{

namespace device
{

namespace cuda
{

namespace detail
{

//template <class ForwardIterator, class BooleanType>
//struct lower_bound_postprocess
//{
//};
//
//template <class ForwardIterator>
//struct lower_bound_postprocess<ForwardIterator, thrust::detail::true_type>
//{
//    template <class DeviceIterator>
//        __device__
//    typename thrust::iterator_traits<DeviceIterator>::difference_type operator()(DeviceIterator final, DeviceIterator begin){
//        return final - begin;
//    }
//};
//
//template <class ForwardIterator>
//struct lower_bound_postprocess<ForwardIterator, thrust::detail::false_type>
//{
//    template <class DeviceIterator>
//        __device__
//    ForwardIterator operator()(DeviceIterator final, DeviceIterator begin){
//        return ForwardIterator(final);
//    }
//};

/////////////////////////////////
// Per-thread search functions //
/////////////////////////////////

template <class RandomAccessIterator, class T, class StrictWeakOrdering>
__device__
RandomAccessIterator __lower_bound(RandomAccessIterator begin, 
                                   RandomAccessIterator end, 
                                   const T& value,
                                   StrictWeakOrdering comp)
{
    typedef typename thrust::iterator_traits<RandomAccessIterator>::difference_type difference_type;

    difference_type len = end - begin;
    difference_type half;

    RandomAccessIterator middle;

    while (len > 0)
    {
        half = len >> 1;
        middle = begin;
        middle += half;

        if (comp(thrust::detail::device::dereference(middle), value)) {
            begin = middle;
            ++begin;
            len = len - half - 1;
        } else {
            len = half;
        }
    }

    return begin;
}

template <class RandomAccessIterator, class T, class StrictWeakOrdering>
__device__
RandomAccessIterator __upper_bound(RandomAccessIterator begin, 
                                   RandomAccessIterator end, 
                                   const T& value,
                                   StrictWeakOrdering comp)
{
    typedef typename thrust::iterator_traits<RandomAccessIterator>::difference_type difference_type;

    difference_type len = end - begin;
    difference_type half;

    RandomAccessIterator middle;

    while (len > 0)
    {
        half = len >> 1;
        middle = begin;
        middle += half;

        if (comp(value, thrust::detail::device::dereference(middle))) {
            len = half;
        } else {
            begin = middle;
            ++begin;
            len = len - half - 1;
        }
    }

    return begin;
}


// short names to avoid nvcc bug
struct lbf
{
    template <class RandomAccessIterator, class T, class StrictWeakOrdering>
        __device__
        typename thrust::iterator_traits<RandomAccessIterator>::difference_type
     operator()(RandomAccessIterator begin, RandomAccessIterator end, const T& value, StrictWeakOrdering comp){
         return __lower_bound(begin, end, value, comp) - begin;
     }
};

struct ubf
{
    template <class RandomAccessIterator, class T, class StrictWeakOrdering>
        __device__
        typename thrust::iterator_traits<RandomAccessIterator>::difference_type
     operator()(RandomAccessIterator begin, RandomAccessIterator end, const T& value, StrictWeakOrdering comp){
         return __upper_bound(begin, end, value, comp) - begin;
     }
};

struct bsf
{
    template <class RandomAccessIterator, class T, class StrictWeakOrdering>
        __device__
     bool operator()(RandomAccessIterator begin, RandomAccessIterator end, const T& value, StrictWeakOrdering comp){
         RandomAccessIterator iter = __lower_bound(begin, end, value, comp);
         return iter != end && !comp(value, thrust::detail::device::dereference(iter));
     }
};


// TODO
// step 0: precache selected elements from [begin, end) into [s_begin, s_end)
// step 1: narrow(s_begin, s_end, value, comp) -> s_begin, s_end
// step 2: output = search(new_begin, new_end, value, comp)

template <class RandomAccessIterator, class InputIterator, class OutputIterator, class StrictWeakOrdering, class BinarySearchFunction>
__global__
void binary_search_kernel(RandomAccessIterator begin, 
                          RandomAccessIterator end,
                          InputIterator values_begin,
                          InputIterator values_end,
                          OutputIterator output,
                          StrictWeakOrdering comp,
                          BinarySearchFunction func)
{
    const unsigned int grid_size = blockDim.x * gridDim.x;
    const unsigned int thread_id = blockIdx.x * blockDim.x + threadIdx.x;

    values_begin += thread_id;
    output       += thread_id;
    
    while (values_begin < values_end){
        thrust::detail::device::dereference(output) = func(begin, end, thrust::detail::device::dereference(values_begin), comp);
        output       += grid_size;
        values_begin += grid_size;
    }
}


// Vector Implementation
template <class ForwardIterator, class InputIterator, class OutputIterator, class StrictWeakOrdering, class BinarySearchFunction>
OutputIterator binary_search(ForwardIterator begin, 
                             ForwardIterator end,
                             InputIterator values_begin, 
                             InputIterator values_end,
                             OutputIterator output,
                             StrictWeakOrdering comp,
                             BinarySearchFunction func)
{
    if (values_begin == values_end) return output;  //empty range

    const size_t BLOCK_SIZE = 256;
    const size_t MAX_BLOCKS = thrust::experimental::arch::max_active_threads()/BLOCK_SIZE;
    const size_t NUM_BLOCKS = std::min(MAX_BLOCKS, ( (values_end - values_begin) + (BLOCK_SIZE - 1) ) / BLOCK_SIZE);

    binary_search_kernel<<<NUM_BLOCKS, BLOCK_SIZE>>>
        (begin, end, values_begin, values_end, output, comp, func);

    return output + (values_end - values_begin); 
}

   

// Scalar Implementation
template <class OutputType, class ForwardIterator, class T, class StrictWeakOrdering, class BinarySearchFunction>
OutputType binary_search(ForwardIterator begin,
                         ForwardIterator end,
                         const T& value, 
                         StrictWeakOrdering comp,
                         BinarySearchFunction func)
{
    // use the vectorized path to implement the scalar version

    // allocate device buffers for value and output
    thrust::detail::raw_device_buffer<T>          d_value(1);
    thrust::detail::raw_device_buffer<OutputType> d_output(1);

    // copy value to device
    d_value[0] = value;

    // perform the query
    detail::binary_search(begin, end, d_value.begin(), d_value.end(), d_output.begin(), comp, func);

    // copy result to host and return
    return d_output[0];
}
   
} // end namespace detail


//////////////////////
// Scalar Functions //
//////////////////////

template <class ForwardIterator, class T, class StrictWeakOrdering>
ForwardIterator lower_bound(ForwardIterator begin,
                            ForwardIterator end,
                            const T& value, 
                            StrictWeakOrdering comp)
{
    typedef typename thrust::iterator_traits<ForwardIterator>::difference_type difference_type;
    
    return begin + detail::binary_search<difference_type>(begin, end, value, comp, detail::lbf());
}


template <class ForwardIterator, class T, class StrictWeakOrdering>
ForwardIterator upper_bound(ForwardIterator begin,
                            ForwardIterator end,
                            const T& value, 
                            StrictWeakOrdering comp)
{
    typedef typename thrust::iterator_traits<ForwardIterator>::difference_type difference_type;
    
    return begin + detail::binary_search<difference_type>(begin, end, value, comp, detail::ubf());
}

template <class ForwardIterator, class T, class StrictWeakOrdering>
bool binary_search(ForwardIterator begin,
                   ForwardIterator end,
                   const T& value, 
                   StrictWeakOrdering comp)
{
    return detail::binary_search<bool>(begin, end, value, comp, detail::bsf());
}


//////////////////////
// Vector Functions //
//////////////////////

template <class ForwardIterator, class InputIterator, class OutputIterator, class StrictWeakOrdering>
OutputIterator lower_bound(ForwardIterator begin, 
                           ForwardIterator end,
                           InputIterator values_begin, 
                           InputIterator values_end,
                           OutputIterator output,
                           StrictWeakOrdering comp)
{
    return detail::binary_search(begin, end, values_begin, values_end, output, comp, detail::lbf());
}

template <class ForwardIterator, class InputIterator, class OutputIterator, class StrictWeakOrdering>
OutputIterator upper_bound(ForwardIterator begin, 
                           ForwardIterator end,
                           InputIterator values_begin, 
                           InputIterator values_end,
                           OutputIterator output,
                           StrictWeakOrdering comp)
{
    return detail::binary_search(begin, end, values_begin, values_end, output, comp, detail::ubf());
}

template <class ForwardIterator, class InputIterator, class OutputIterator, class StrictWeakOrdering>
OutputIterator binary_search(ForwardIterator begin, 
                             ForwardIterator end,
                             InputIterator values_begin, 
                             InputIterator values_end,
                             OutputIterator output,
                             StrictWeakOrdering comp)
{
    return detail::binary_search(begin, end, values_begin, values_end, output, comp, detail::bsf());
}

} // end namespace cuda

} // end namespace device

} // end namespace detail

} // end namespace thrust

#endif // __CUDACC__

