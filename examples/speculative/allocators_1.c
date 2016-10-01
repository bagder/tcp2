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
 * Modified operations.  The application author may set these functions when
 * they wish to take responsibility for allocating non tcp2 structures or
 * memory regions, which are those structs or memory regions with type id
 * == 0 or > the tcp2 type limit (1048576)
 */
static struct tcp2_allocator_operations tcp2_trivial_allocator_app_operations = {
  .alloc = NULL,
  .free = NULL,
};

void tcp2_set_trivial_allocator_app_operations(
    void *(*alloc)(const struct tcp2_allocator *allocator,
                   uint64_t type, size_t size),
    void   (*free)(const struct tcp2_allocator *allocator,
                   uint64_t type, size_t size, void *obj)) {
  tcp2_trivial_allocator_app_operations.alloc = alloc;
  tcp2_trivial_allocator_app_operations.free = free;
}

void tcp2_clear_trivial_allocator_app_operations(void) {
  tcp2_trivial_allocator_app_operations.alloc = NULL;
  tcp2_trivial_allocator_app_operations.free = NULL;
}



/*
 * A trivial allocator implementation that simply uses system malloc and and
 * free.
 */

/*
 * The definitions of the trivial alloc and free functions.
 */
static void *tcp2_trivial_alloc(const struct tcp2_allocator *allocator,
                                uint64_t type, size_t size) {
  if ((tcp2_trivial_allocator_app_operations.alloc != NULL) &&
      (type == 0) || (type > 1048576)) {
    return
      tcp2_trivial_allocator_app_operations.alloc(allocator, type, size);
  }

  void *obj = malloc(size);
  if (!obj)
    return NULL;

  if (type != 0)
    memset(obj, 0, size);

  return obj;
}

static void tcp2_trivial_free(const struct tcp2_allocator *allocator,
                              unt64_t type, size_t size, void *obj) {
  if ((tcp2_trivial_allocator_app_operations.alloc != NULL) &&
      (type == 0) || (type > 1048576)) {
    tcp2_trivial_allocator_app_operations.free(
      allocator, type, size, obj);

    return;
  }

  if (type != 0)
    memset(obj, 0, size);

  free(obj);
}



/*
 * The global operations structure to hold references to trivial alloc and free.
 */
static struct tcp2_allocator_operations tcp2_trivial_allocator_operations = {
  .alloc = tcp2_trivial_alloc,
  .free = tcp2_trivial_free,
};

static struct tcp2_allocator tcp2_trivial_allocator = {
  .operations = &tcp2_trivial_allocator_operations,
};



/*
 * Get the built in trivial allocator.
 *
 * Using this function, the allocator can be supplied as a parameter to other
 * functions.
 */
const struct tcp2_allocator *tcp2_get_trivial_allocator(void) {
  return &tcp2_trivial_allocator;
}






/*
 * This is an example of an application modifying the trivial allocator to
 * enact some small changes to its behaviour.
 */
static void *app_modified_alloc(const struct tcp2_allocator *allocator,
                                uint64_t type, size_t size) {
  if (type == APP_TYPE1)
    return app_alloc_type1();
  else
  if (type == APP_TYPE2)
    return app_alloc_type2();

  return tcp2_trivial_alloc(allocator, type, size);
}

static void app_modified_free(const struct tcp2_allocator *allocator,
                              unt64_t type, size_t size, void *obj) {
  if (type == APP_TYPE1)
    return app_free_type1();
  else
  if (type == APP_TYPE2)
    return app_free_type2();

  return tcp2_trivial_alloc(allocator, type, size);
}



/*
 * Now use the tcp2 trivial allocator interfaces to set the modified alloc and
 * free.
 */
int main(int argc, char** argc) {
  tcp2_set_trivial_allocator_app_operations(
    &app_modified_alloc, &app_modified_free);

  int retval = app_run();

  tcp2_clear_trivial_allocator_app_operations();

  return retval;
}






/*
 * This is an example of a more complex allocator, although the internals of
 * how it optimises and performs allocations will be omitted.
 *
 * During creation, initialisation and destruction, the custom allocator will
 * fall back to the trivial allocator.
 */
struct app_custom_allocator {
  /*
   * Because the tcp2_allocator is the first member of the custom allocator,
   * it's offset is zero therefore it's address is the same as it's parents'.
   */
  struct tcp2_allocator tcp2_allocator;

  /*
   * Magic application specific allocator data structures here:
   */
  struct app_custom_resource1 {
  };

  struct app_custom_resource2 {
  };
};

static void *app_custom_allocator_alloc(
    const struct tcp2_allocator *allocator,
    uint64_t type, size_t size) {
  /*
   * Upcast the tcp2 allocator to the application specific allocator.
   */
  const struct app_custom_allocator *app_custom_allocator =
    (const struct app_custom_allocator *)allocator;

  /*
   * Do magic application specific allocation using custom allocator resources.
   */

  return obj;
}

static void app_custom_allocator_free(
    const struct tcp2_allocator *allocator,
    uint64_t type, size_t size, void *obj) {
  /*
   * Upcast the tcp2 allocator to the application specific allocator.
   */
  const struct app_custom_allocator *app_custom_allocator =
    (const struct app_custom_allocator *)allocator;

  /*
   * Return the memory address to the custom allocator using magic.
   */
}

/*
 * The global operations structure to hold references to custom alloc and
 * free.
 */
static struct tcp2_allocator_operations app_custom_allocator_operations = {
  .alloc = app_custom_alloc,
  .free = app_custom_free,
};



/*
 * Create a custom allocator.  Not a 'get' as this example allows multiple
 * custom allocation contexts, for example one may be created per application
 * thread.
 */
struct app_custom_allocator *app_create_custom_allocator() {
  const struct app_custom_allocator *app_custom_allocator =
    tcp2_allocator_alloc(tcp2_get_trivial_allocator(),
                         0, sizeof(struct app_custom_allocator));
  if (!app_custom_allocator) {
    return NULL;
  }

  app_custom_allocator->tcp2_allocator.operations =
    &app_custom_allocator_operations;

  app_initialise_custom_allocator(app_custom_allocator);

  return app_custom_allocator;
}

/*
 * Custom allocator destructor.
 */
void app_destroy_custom_allocator(
    struct app_custom_allocator *app_custom_allocator) {
  app_cleanup_custom_allocator(app_custom_allocator);

  tcp2_allocator_free(tcp2_get_trivial_allocator(),
                      0, sizeof(struct app_custom_allocator),
                      app_custom_allocator);
}






/*
 * Next is a demonstration of how an allocator may be provided to tcp2 at
 * runtime.
 */
void app_on_thread_start() {
#ifdef USE_TRIVIAL
  struct tcp2_thread_context *tcp2_thread_context =
    tcp2_create_thread_context(tcp2_system_context,
                               tcp2_get_trivial_allocator());
#else if USE_MODIFIED
  struct tcp2_thread_context *tcp2_thread_context =
    tcp2_create_thread_context(tcp2_system_context,
                               app_get_modified_allocator());
#else
  struct tcp2_thread_context *tcp2_thread_context =
    tcp2_create_thread_context(tcp2_system_context,
                               app_create_custom_allocator());
#endif

  app_store_tcp2_thread_context(tcp2_thread_context);

  app_execute_thread_loop();
}
