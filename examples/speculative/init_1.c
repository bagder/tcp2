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
 * This particular case study demonstrates some ideas for initialisation of
 * the tcp2 library within a parent application.  Two aspects will be focussed
 * on:
 * - Function: given the (at this moment: imaginary) features that tcp2
 *   what interfaces will be needed and how will these be initialised in
 *   the various phases of the applications' lifecycle
 * - Form: given the functions mentioned above, what will the API look like?
 *   what are the return values from initialisation and how will they need
 *   to be carried around by the application
 *
 * The main concern of 
 * Assumptions:
 * - The activity of the application itself will be largely ignored, the focus
 *   will be on initialisation
 * - Although this example application is multi-threaded, there will be one
 *   decision path that demonstrates how a single threaded application wil
 *   make use of tcp2:
 * ----BEGIN DISCUSSION----
 * This philosophy of thread initialisation in applications is a key discussion
 * point.  In this case study, the following is proposed:
 * - There is a difference between system context initialisation and per-thread
 *   context initialisation
 * - System context is the container of global system state, for example: the
 *   master lookup table of connections, indexed by connection id.  System
 *   context is represented by a data structure that should be:
 *   - retained by the application using some pointer variable, easily
 *     accessible either directly or through the thread context (see below) 
 *   - should be used explicitly as a parameter to tcp2 API functions that
 *     act at ths system level, or used implicitly when invoking per-thread
 *     API functions, since the system context will be referenced by the
 *     per-thread context
 *   This design acts as a tradeoff between the need to maintain global system
 *   state through a singleton structure, but at the same time have a clear
 *   handle to this state (the pointer to the system context object) that has
 *   clear lifecycle and ownership, rather than use some magic static
 *   functions, which is what most opponents of the singleton pattern disagree
 *   with
 * - Thread context is the container for thread local data objects, that can
 *   be safely accessed without locking as all events specific to those objects
 *   should be queued to that same thread.  Examples of some thread specific
 *   structures include:
 *   - Connection related data - as all events relating to a connection should
 *     be handled in the same thread once a connection is associated with that
 *     thread
 *   - Pre-allocated memory blocks, something like slabs, that can be retained
 *     in a per-thread structure and have their memory blocks easily retrieved
 *     and returned without locking
 * ----END DISCUSSION----
 */

/*
 * main:
 *
 * Entry point of the application.  This is where the notion of 'system wide'
 * context is introduced.  The notion of per-thread initilisation will be
 * alluded to here then demonstrated in a later function.  Although this case
 * study application is primarily multi-threaded, a single threaded application
 * will behave in the same ways as when concurrency == 1 (the lowest possible
 * value in this application).
 *
 * The host application uses a 'magic' system of hidden objects and global
 * functions that 'just work' and 'do the right thing' when called from
 * different threads.  I wanted this to serve as a counter example: an example
 * of how I DON'T want the tcp2 library to behave.
 */
void main(int argc, char **argv) {
  app_startup();

  app_parse_options(argc, argv);

  /*
   * Here is where the tcp2 system context is first initialised.  This object
   * is the handle to global shared state of tcp2, including the registry of
   * thread context objects that will be created on a per thread basis.  All
   * thread contexts will hold a reference to the system context and will make
   * use of global state where necessary and where thread local resources are
   * not available.
   */
  struct tcp2_system_context *tcp2_system_context =
    tcp2_create_system_context();
 
  app_store_tcp2_system_context(tcp2_system_context);

  for (int concurrency_counter = 1;
       concurrency_counter < app_system_context->options.concurrency;
       ++concurrency_counter) {
    app_create_thread(&app_on_thread_start);
  }

  app_on_thread_start();

  app_wait_threads();

  int return_value = app_get_return_value();

  app_cleanup();

  return return_value;
}



/*
 * app_on_thread_start
 *
 * The entry point for the creation of new thread contexts and the execution
 * of per thread event loops.
 *
 * This can be invoked either directly in the case of the main thread, or as
 * a callback from app_create_thread, in which case this functon will be
 * called in the context of the newly created thread.
 */
void app_on_thread_start() {
  /*
   * Magically retrieve the system context
   */
  struct tcp2_system_context *tcp2_system_context =
    app_retrieve_tcp2_system_context();

  /*
   * Use the system context to create a thread context.  The system context is
   * referred to by the thread context for times when global variables need to
   * be accessed (in a thread safe way)  An example of such state includes the
   * master registry of all connection ids.
   */
  struct tcp2_thread_context *tcp2_thread_context =
    tcp2_create_thread_context(tcp2_system_context);

  /*
   * Store the tcp2 thread context in a thread local store.  As many runtime
   * state and resources as possible will be stored in the thread contexts so
   * that they may be accessed without locking.  Host applications will need
   * to participate in this optimisation, for example, an application should
   * aim to deliver all udp packets in a same connection to a same thread.
   */
  app_store_tcp2_thread_context(tcp2_thread_context);

  app_execute_thread_loop();
}












































