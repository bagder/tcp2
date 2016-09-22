/*
 * Copyright (c) 2016 Nick Jones <nick.fa.jones@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * This case study serves as a demonstration of an application that makes use
 * of the tcp2 library.  It is constructed with 'mostly' syntactically correct
 * C code but with many dependencies left out and many functions, both of the
 * application and the tcp2 library, left referred to yet undefined.
 *
 * The purpose is to demonstrate ideas about the form and function of the tcp2
 * API, of what features it will provide, of what inputs it will receive, of
 * what outputs it will produce, of the granularity the API functions will be
 * and how they will be called from an application.
 *
 * The form and function of the application itself is also an important aspect
 * of the case study, as it provides an example of a kind of application tcp2
 * will be used in and the various situations and program runtime environments
 * that tcp2 may need to support.
 *
 * Parts of the comments in the case study code may be marked with:
 * ----BEGIN DISCUSSION----
 * ----END DISCUSSION----
 * These sections indicate areas where important design or philisophical
 * decisions have been made for the tcp2 specific interfaces or behaviour in
 * order to fit into the case study but are significant enough to warrant
 * additional discussion.
 *
 * However, almost all parts of the case study should act as motivation for
 * discussion.
 */

/*
 * This case study demonstrates ideas about how memory allocation can be a
 * consideration of the tcp2 library.
 *
 * The fundamental idea is that memory allocation is pluggable, meaning
 * allocation events are carried out through callbacks, which can be overloaded
 * by the application author in order to:
 * - add optimisations
 * - better control memory allocation rates
 * - set limits on allocations in order to avoid memory blowout and general
 *   system overload
 * - assist debugging
 * - produce statistics or telemetry
 *
 * Although open source and proprietary pluggable allocators exist, for
 * example: jemalloc and tcmalloc, and these allocators may do a superior job
 * for general purpose allocation, the tcp2 allocator layer allows the
 * application author to make use of precise control of tcp2 memory usage.
 * Importantly, the tcp2 allocation layer can also, optionally, add nothing.
 *
 * The proposed method of allowing allocator customisation is by exposing some
 * of the internals of the allocation system through an operations functions
 * structure: tcp2_allocator_operations.  An application may create new
 * versions of these operations functions and populate it's own copy of a
 * tcp_allocator_operations structure with these, then by providing this
 * structure to tcp2 as an initialisation parameter memory allocation will now
 * significantly belong to the application.
 */



/*
 * The following structures and functions are declared by tcp2, they represent
 * the interface to the pluggable memory allocation system.
 */



/*
 * Allocator.
 *
 * The allocator structure serves two purposes:
 * - Maintain an allocator operations structure that holds the specific
 *   implementations of the alloc and free functions
 * - Provide a vehicle for a more complex state structure to be provided to
 *   the alloc and free functions.
 *
 * An application author may implement a custom memory allocation system based
 * on a complex structure that maintains state and resources to be used during
 * memory allocation.  The first member of this complex structure should be
 * a tcp2 allocator structure and the alloc and free function pointers of the
 * tcp2 allocator operations set to point to the application authors specific
 * custom implementations of alloc and free.
 *
 * By upcsting (referencing the complex parent structure using the address and
 * data type of its first member) the complex object can be indirectly supplied
 * to the tcp2 library through regular initialisation interfaces.  Then when
 * the custom alloc or free functions are invoked, they can downcast (child
 * structure, which is a first member, is cast back to the parent) and used
 * directly in the function.
 */
struct tcp2_allocator {
  struct tcp2_allocator_operations *operations;
};



/*
 * Allocator Operations.
 *
 * The essential operations executed within the allocator system.  Here there
 * are only two: alloc and free:
 */
struct tcp2_allocator_operations {
/*
 * Allocate a memory region to the tcp2 library for use as a known data
 * object.
 *
 * Arguments:
 * allocator: a pointer to a tcp2_allocator, possiibly embedded
 *
 * type: The type of object that tcp2 needs to allocate.  tcp2 internally will
 *       primarily deal with a few entirely known data types, and only in a few
 *       cases with dynamically sized memory regions.
 *
 *       All of these known tcp2 data types will be assigned a unique id that
 *       will be passed to the allocator to provide it with additional
 *       information.  This will allow an allocator implementation to perform
 *       optimisations such as object pooling and sl*bing and also to allow
 *       collection of statistics and other metrics.
 *
 *       Type ids will be defined in header files alongside their object
 *       definitions, will be positive, greater than zero and unique within
 *       tcp2.
 *
 *       The id number: '0' indicates a request for allocation of memory that
 *       is either of a data type not belonging to tcp2, or a dynamically sized
 *       region, for example a packet body.
 *
 *       tcp2 will only use id numbers below: 1048576  Applications may use ids
 *       from this value and beyond if they wish to take advantage of the tcp2
 *       memory allocation interface.
 *
 * size: the size in bytes of the memory region requested by tcp2, will always
 *       be repeated in addition to a known type id.
 *
 * Returns:
 * A pointer to a memory region that is either:
 * - Sized equal to or greater than the requested size
 * - NULL, upon failure to allocate the memory region
 */
  void *(*alloc)(const struct tcp2_allocator *allocator,
                 uint64_t type, size_t size);

/*
 * Free a memory region, returning it from use within tcp2 back to the
 * allocator.
 *
 * Arguments:
 * allocator: As above.
 *
 * type: As above.
 *
 * size: As above.
 *
 * obj : A pointer to the memory region that is to be returned to the
 *       allocator.
 */
  void  (*free)(const struct tcp2_allocator *allocator,
                uint64_t type, size_t size, void *obj);
};



/*
 * Convenient helper functions for the tcp2_allocator alloc and free.
 */
void *tcp2_allocator_alloc(const struct tcp2_allocator *allocator,
                           uint64_t type, size_t size) {
  return allocator->operations->alloc(allocator, type, size);
  }

void tcp2_allocator_free(const struct tcp2_allocator *allocator,
                         uint64_t type, size_t size, void *obj) {
  allocator->operations->free(allocator, type, size, obj);
  }



/*
 * A simple allocator implementation that simply uses system malloc and and
 * free.
 */



/*
 * NOTE: the following functions and objects are static and hidden in 
 * a '.c' file
 */
static void *tcp2_trivial_alloc(const struct tcp2_allocator *allocator,
                                uint64_t type, size_t size) {
  void *obj = malloc(size);
  if (!obj)
    return NULL;

  if (type != 0)
    memset(obj, 0, size);

  return obj;
  }

static void tcp2_trivial_free(const struct tcp2_allocator *allocator,
                              unt64_t type, size_t size, void *obj) {
  if (type != 0)
    memset(obj, 0, size);

  free(obj);
  }

static struct tcp2_allocator_operations tcp2_trivial_allocator_operations = {
   .alloc = tcp2_trivial_alloc,
   .free = tcp2_trivial_free,
};

static struct tcp2_allocator tcp2_trivial_allocator = {
  .operations = &tcp2_trivial_allocator_operations,
};
/*
 * END NOTE
 */



/*
 * Get the built in trivial allocator.
 *
 * Using this function, the allocator can be supplied as a parameter to other
 * functions.
 */
const struct tcp2_allocator *tcp2_get_trivial_allocator(void) {
  return &tcp2_trivial_allocator;
}









































































