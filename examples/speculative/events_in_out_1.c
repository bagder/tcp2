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
 * This particular case study is to demonstrate the two-way requirement of
 * tcp2: to both receive input from the application and how tcp2 may provide
 * feedback or output back to the application.
 * Inputs include:
 * - UDP packet data
 * - timeout notifications
 * Outputs and feedback:
 * - Internally generated UDP packets as per the quic protocol standard
 * - requests for timeout notifications
 *
 * Assumptions:
 * - The application performs non-blocking IO and events are queued and
 *   delivered using callbacks from a hidden mechanism known to the
 *   application, such as a wrapper around EV or libevent
 * - Events demonstrated here are: network reads of UDP data and timeouts
 * - The threading model of the application is undefined so don't assume too
 *   much about it in this case study.  The case study mentions an app_context
 *   object, but the threading scope of this object should be though of as
 *   undefined here.  Additionally, the threading scope of the tcp2_context
 *   should also remain undefined here.
 *   The topic of threading is quite large in itself and will require
 *   further design discussion.
 *   More details on the topic of threading will be presented in other case
 *   studies.
 * - Buffering: the subject of buffering data also deserves its own set of
 *   case studies as it is such an important topic in the context of any
 *   networked or I/O intensive system.
 *   For now, the following assumptions will be made in regard to buffering:
 *   - A struct with corresponding functions (class) called 'tcp2_buffer' is
 *     used to represent data.  Don't worry about the implementation details
 *     of this for now.
 *   ----BEGIN DISCUSSION----
 *   - The tcp2 library does not construct buffers, either those containing
 *     input, or those destined to contain output, it is provided with these
 *     by the application
 *   ----END DISCUSSION----
 *   - The sample application conveniently uses the tcp2_buffer interface
 *   More detailed studies on the topic of buffering will be presented in
 *   other case studies.
 */

/*
 * app_network_on_udp_read:
 *
 * The application receives UDP data from its network layer, conveniently
 * packaged up in a buffer class.  There may be multiple packets contained in
 * the buffer.
 */
void app_network_on_udp_read(struct app_context *app_context,
                             struct tcp2_buffer *buffer_in) {
  /*
   * extract a tcp2 context object from the application context.  Once again,
   * the scope of the tcp2 context is undefined in regard to threading, that
   * is for another case study to cover.
   */
  struct tcp2_context *tcp2_context = app_get_tcp2_context(app_context);

  /*
   * Prepare a tcp2 events structure.  This is used to both feed events in to
   * tcp2, and receive events and information that comes out.
   *
   * The fields of tcp2_events are:
   * buffer_in: when udp data arrives into the system, tcp2 needs to be told
   *            about it, so set the 'in_buffer' field, a pointer to a buffer,
   *            to point to the newly received udp packets.
   * buffer_out: after processing events fed in from the application, the tcp2
   *             'engine' may produce udp packet data that should be sent back
   *             to the opposite endpoint, eg a server hello in response to a
   *             client hello.
   * timeout_out: The tcp2 library will keep in internal chain of time
   *              differentiated events for each tcp2_context.  Time
   *              differentiation may be needed for a number of reasons:
   *              - Timing of output packets, eg. timing of delayed responses
   *                for client version mismatch penalty or send delay in
   *                response to congestion control strategy
   *              - Timing of internal maintenance activities, eg. checking
   *                for time passed since last ack for connections
   *              - Even timing of processing of input packets for control of
   *                server workload
   *              The tcp2 library will notify the application when its next
   *              event is scheduled to take place (in relation to current
   *              time) using the timeout_out field (which is something like a
   *              'struct timeval')
   *              ----BEGIN DISCUSSION----
   *              The tcp2 library may take a number of approaches to setting
   *              this field, which is a design discussion in itself:
   *              - Always return the time until the next scheduled event or
   *                {0, 0} of no events are pending
   *              - Return the time until the next scheduled event ONLY if the
   *                first event on the list was changed.  This works on the
   *                assumption that the application has already scheduled a
   *                timeout for the 'previous next event' and that timeout
   *                hasn't fired yet.  During processing of input, new events
   *                may have been added to the internal event list, but none
   *                of them were 'closer' than the 'previous next event',
   *                therefore the application need not update its timeout,
   *                and {0, 0} is returned.  However, if a new event was added
   *                to the head of the event list meaning it is 'closer' than
   *                the 'previous next event', then the relative time to that
   *                event is returned to the application.  Again, if no events
   *                are scheduled, return {0, 0}
   *              ----END DISCUSSION----
   */
  struct tcp2_events tcp2_events;
  tcp2_events.buffer_in = buffer_in;
  tcp2_events.buffer_out = tcp2_create_buffer();
  tcp2_events.timeout_out = {0, 0};

  /*
   * Invoke the tcp2 internal event processing loop, in this case to process
   * new packets of udp data.
   * This activity is non blocking, the tcp2 library should not perform any
   * socket I/O and ideally shouldn't perform any block device I/O either, nor
   * should it call any system calls that could be prone to delay in the
   * kerel. All outputs relevant to the application will be placed in the
   * tcp2_events *_out members.
   *
   * ----BEGIN DISCUSSION----
   * Nice to have: a deadline timer, application can provide a relative time
   * in the form of something like a 'struct timeval' that indicates the
   * maximum time tcp2_process should spend working.  If processing events
   * takes too long, tcp2_process will return at a correct time.  Internally,
   * the state of the event queue should be remain intact so that tcp2_process
   * can be called again and simply pick up where it left off.
   * ----END DISCUSSION----
   */
  tcp2_process(tcp2_context, &tcp2_events);

  /*
   * Check to see if the tcp2 library has any new timeouts for us.  This
   * involves calling a 'magic' application function that 'does the right
   * thing'
   */
  if (!app_timer_keep_old_timeout(app_context, &tcp2_events.timeout_out)) {
    app_timer_schedule(app_context,
                       &tcp2_events.timeout_out,
                       &app_timer_on_timeout);
  }

  /*
   * Check to see if the tcp2 library has produced any packets that need to be
   * sent to another endpoint.
   * The application can magically infer the address and port of the recipient
   * from the buffer object but only in this case study.  Other case studies
   * will explore how addressing will be handled.
   */
  if (!tcp2_buffer_empty(events.out_buffer)) {
    app_network_write_udp(app_context, events.buffer_out);
  /*
   * buffer_out is now property of the app's network layer
   */
    events.buffer_out = NULL;
  }

  /*
   * Clean up the events structure.
   */
  events.buffer_in = NULL;

  if (events.buffer_out != NULL) {
    tcp2_destroy_buffer(events.buffer_out);
    events.buffer_out = NULL;
  }

  events.timeout_out = {0, 0};

  /*
   * Prepare for more udp packet reads from the network layer
   */
  app_network_read_udp(app_context, in_buffer, &app_network_on_udp_read);
}


/*
 * app_timer_on_timeout
 *
 * A timeout that the application has previously scheduled has now passed and
 * notification is send in the form of a call to this callback function.
 */
void app_timer_on_timeout(struct app_context *app_context) {
  struct tcp2_context *tcp2_context = app_get_tcp2_context(app_context);

  /*
   * Prepare the tcp2_events, this time there is no data in, but as always
   * there may be data out.
   */
  struct tcp2_events tcp2_events;
  tcp2_events.buffer_in = NULL;
  tcp2_events.buffer_out = tcp2_create_buffer();
  tcp2_events.timeout_out = {0, 0};

  tcp2_process(tcp2_context, &tcp2_events);

  /*
   * Check to see if the tcp2 library has any new timeouts for us.  This
   * involves calling a 'magic' application function that: 'does the right
   * thing'
   */
  if (!app_timer_keep_old_timeout(app_context, &tcp2_events.timeout_out)) {
    app_timer_schedule(app_context,
                       &tcp2_events.timeout_out,
                       &app_timer_on_timeout);
  }

  /*
   * Check to see if the tcp2 library has produced any packets that need to be
   * sent to another endpoint.
   * The application can magically infer the address and port of the recipient
   * from the buffer object but only in this case study.  Other case studies
   * will explore how addressing will be handled.
   */
  if (!tcp2_buffer_empty(events.out_buffer)) {
    app_network_write_udp(app_context, events.buffer_out);
  /*
   * buffer_out is now property of the app's network layer
   */
    events.buffer_out = NULL;
  }

  /*
   * Clean up the events structure.
   */
  events.buffer_in = NULL;

  if (events.buffer_out != NULL) {
    tcp2_destroy_buffer(events.buffer_out);
    events.buffer_out = NULL;
  }

  events.timeout_out = {0, 0};
}
