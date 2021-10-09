# Slab & Buddy Linux-like Memory Allocators  

A Linux-like slab allocator that has underlying buddy allocator.

## Description

The basic idea behind the slab allocator is to have caches of commonly used objects kept in an initialised state available for use by the kernel. Without an object based allocator, the kernel will spend much of its time allocating, initialising and freeing the same object. The slab allocator aims to to cache the freed object so that the basic structure is preserved between uses.

This project has next principle aims:
 * The allocation of small blocks of memory to help eliminate internal fragmentation that would be otherwise caused by the buddy system.
 * The caching of commonly used objects so that the system does not waste time allocating, initialising and destroying objects.

## Acknowledgments

Main project inspiration
* [kernel.org/slab](https://www.kernel.org/doc/gorman/html/understand/understand011.html)
