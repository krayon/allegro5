/*         ______   ___    ___
 *        /\  _  \ /\_ \  /\_ \
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *      Asynchronous event processing with SIGALRM.
 *
 *      By Michael Bukin.
 *
 *      See readme.txt for copyright information.
 */


#include "allegro.h"
#ifdef ALLEGRO_QNX
#include "allegro/aintqnx.h"
#else
#include "allegro/aintunix.h"
#endif


#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <limits.h>


static int _sigalrm_already_installed = FALSE;
static volatile int _sigalrm_interrupt_pending = FALSE;
static volatile int _sigalrm_cli_count = 0;
static volatile int _sigalrm_abort_requested = FALSE;
static volatile int _sigalrm_aborting = FALSE;
static void (*_sigalrm_interrupt_handler)(unsigned long interval) = 0;
void (*_sigalrm_digi_interrupt_handler)(unsigned long interval) = 0;
void (*_sigalrm_midi_interrupt_handler)(unsigned long interval) = 0;
static RETSIGTYPE (*_sigalrm_old_signal_handler)(int num) = 0;

static RETSIGTYPE _sigalrm_signal_handler(int num);



/* _sigalrm_disable_interrupts:
 *  Disables "interrupts".
 */
void _sigalrm_disable_interrupts(void)
{
   _sigalrm_cli_count++;
}



/* _sigalrm_enable_interrupts:
 *  Enables "interrupts.".
 */
void _sigalrm_enable_interrupts(void)
{
   if (--_sigalrm_cli_count == 0) {
      /* Process pending interrupts.  */
      if (_sigalrm_interrupt_pending) {
	 _sigalrm_interrupt_pending = FALSE;
	 _sigalrm_signal_handler(SIGALRM);
      }
   }
   else if (_sigalrm_cli_count < 0) {
      abort();
   }
}



/* _sigalrm_interrupts_disabled:
 *  Tests if interrupts are disabled.
 */
int _sigalrm_interrupts_disabled(void)
{
   return _sigalrm_cli_count;
}



static int first_time = TRUE;
static struct timeval old_time;
static struct timeval new_time;
static int paused;



/* _sigalrm_time_passed:
 *  Calculates time passed since last call to this function.
 */
static unsigned long _sigalrm_time_passed(void)
{
   unsigned long interval;

   if (paused) return 0;

   gettimeofday(&new_time, 0);
   if (!first_time) {
      interval = ((new_time.tv_sec - old_time.tv_sec) * 1000000L +
		  (new_time.tv_usec - old_time.tv_usec));
   }
   else {
      first_time = FALSE;
      interval = 0;
   }

   old_time = new_time;

   return interval;
}



/* _sigalrm_do_abort:
 *  Aborts program (interrupts will be enabled on entry).
 */
static void _sigalrm_do_abort(void)
{
   /* Someday, it may choose to abort or exit, depending on user choice.  */
   if (!_sigalrm_aborting) {
      _sigalrm_aborting = TRUE;
      exit(EXIT_SUCCESS);
   }
}



/* _sigalrm_request_abort:
 *  Requests interrupt-safe abort (abort with enabled interrupts).
 */
void _sigalrm_request_abort(void)
{
   if (_sigalrm_interrupts_disabled())
      _sigalrm_abort_requested = TRUE;
   else
      _sigalrm_do_abort();
}



void _sigalrm_pause(void)
{
   if (paused) return;
   paused = TRUE;
}

void _sigalrm_unpause(void)
{
   if (!paused) return;
   gettimeofday(&old_time, 0);
   paused = FALSE;
}



/* _sigalrm_start_timer:
 *  Starts timer in one-shot mode.
 */
void _sigalrm_start_timer(void)
{
   struct itimerval timer;

   _sigalrm_old_signal_handler = signal(SIGALRM, _sigalrm_signal_handler);

   timer.it_interval.tv_sec = 0;
   timer.it_interval.tv_usec = 0;
   timer.it_value.tv_sec = 0;
   timer.it_value.tv_usec = 10000; /* 10 ms */
   setitimer(ITIMER_REAL, &timer, 0);
}



/* _sigalrm_stop_timer:
 *  Stops timer.
 */
void _sigalrm_stop_timer(void)
{
   struct itimerval timer;

   timer.it_interval.tv_sec = 0;
   timer.it_interval.tv_usec = 0;
   timer.it_value.tv_sec = 0;
   timer.it_value.tv_usec = 0;
   setitimer(ITIMER_REAL, &timer, 0);

   signal(SIGALRM, _sigalrm_old_signal_handler);
}



/* _sigalrm_signal_handler:
 *  Handler for SIGALRM signal.
 */
static RETSIGTYPE _sigalrm_signal_handler(int num)
{
   unsigned long interval, i;

   if (_sigalrm_interrupts_disabled()) {
      _sigalrm_interrupt_pending = TRUE;
      return;
   }

   interval = _sigalrm_time_passed();

   while (interval) {
      i = MIN(interval, INT_MAX/(TIMERS_PER_SECOND/100));
      interval -= i;

      i = i * (TIMERS_PER_SECOND/100) / 10000L;

      if (_sigalrm_interrupt_handler)
	 (*_sigalrm_interrupt_handler)(i);

      if (_sigalrm_digi_interrupt_handler)
	 (*_sigalrm_digi_interrupt_handler)(i);

      if (_sigalrm_midi_interrupt_handler)
	 (*_sigalrm_midi_interrupt_handler)(i);
   }

   /* Abort with enabled interrupts.  */
   if (_sigalrm_abort_requested)
      _sigalrm_do_abort();

   _sigalrm_start_timer();
}



/* _sigalrm_init:
 *  Starts interrupts processing.
 */
int _sigalrm_init(void (*handler)(unsigned long interval))
{
   if (_sigalrm_already_installed)
      return -1;

   _sigalrm_already_installed = TRUE;

   _sigalrm_interrupt_handler = handler;
   _sigalrm_interrupt_pending = FALSE;
   _sigalrm_cli_count = 0;

   first_time = TRUE;
   paused = FALSE;

   _sigalrm_start_timer();

   return 0;
}



/* _sigalrm_exit:
 *  Stops interrupts processing.
 */
void _sigalrm_exit(void)
{
   if (!_sigalrm_already_installed)
      return;

   _sigalrm_stop_timer();

   _sigalrm_interrupt_handler = 0;
   _sigalrm_interrupt_pending = FALSE;
   _sigalrm_cli_count = 0;

   _sigalrm_already_installed = FALSE;
}


/*============================================================
 * SIGALRM bg_manager
 *-----------------------------------------------------------*/

static void (*bg_man_sigalrm_handler_chain) (unsigned long);
static int bg_man_sigalrm_installed;

#define MAX_FUNCS 16
static bg_func funcs[MAX_FUNCS];
static int max_func; /* highest+1 used entry */

static void bg_man_sigalrm_handler (unsigned long interval)
{
	int i;
	bg_man_sigalrm_handler_chain (interval);
	for (i = 0; i < max_func; i++)
		if (funcs[i]) funcs[i](0);
}

static int bg_man_sigalrm_init (void)
{
	if (!_sigalrm_already_installed) return -1; // must install sigalrm first
	if (bg_man_sigalrm_installed++) return 0;
	bg_man_sigalrm_handler_chain = _sigalrm_interrupt_handler;
	_sigalrm_interrupt_handler = bg_man_sigalrm_handler;
	return 0;
}

static void bg_man_sigalrm_exit (void)
{
	if (!bg_man_sigalrm_installed) return;
	if (--bg_man_sigalrm_installed) return;
	_sigalrm_interrupt_handler = bg_man_sigalrm_handler_chain;
}

static int bg_man_sigalrm_register_func (bg_func f)
{
	int i;
	for (i = 0; funcs[i] && i < MAX_FUNCS; i++);
	if (i == MAX_FUNCS) return -1;

	funcs[i] = f;

	if (i == max_func) max_func++;

	return 0;
}

static int bg_man_sigalrm_unregister_func (bg_func f)
{
	int i;
	for (i = 0; funcs[i] != f && i < max_func; i++);
	if (i == max_func) return -1;

	funcs[i] = NULL;

	if (i+1 == max_func)
		do {
			max_func--;
		} while (!funcs[max_func] && max_func > 0);

	return 0;
}

struct bg_manager _bg_man_sigalrm = {
	0,
	bg_man_sigalrm_init,
	bg_man_sigalrm_exit,
	bg_man_sigalrm_register_func,
	bg_man_sigalrm_unregister_func
};

