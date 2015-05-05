/* nanokernel dynamic-size LIFO queue object */

/*
 * Copyright (c) 2010-2015 Wind River Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1) Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2) Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3) Neither the name of Wind River Systems nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
DESCRIPTION
This module provides the VxMicro nanokernel (aka system-level) 'lifo'
implementation. This module provides the backing implementation for the
following APIs:

   nano_lifo_init
   nano_fiber_lifo_put, nano_task_lifo_put, nano_isr_lifo_put
   nano_fiber_lifo_get, nano_task_lifo_get, nano_isr_lifo_get
   nano_fiber_lifo_get_wait, nano_task_lifo_get_wait

INTERNAL
In some cases the compiler "alias" attribute is used to map two or more
APIs to the same function, since they have identical implementations.
*/

#include <nanok.h>
#include <toolchain.h>
#include <sections.h>
#include <wait_q.h>

/*******************************************************************************
*
* nano_lifo_init - initialize a nanokernel linked list lifo object
*
* This function initializes a nanokernel system-level linked list lifo
* object structure.
*
* It may be called from either a fiber or task context.
*
* RETURNS: N/A
*
* INTERNAL
* Although the existing implementation will support invocation from an ISR
* context, for future flexibility, this API will be restricted from ISR
* level invocation.
*/

void nano_lifo_init(
	struct nano_lifo *lifo /* lifo to initialize */
	)
{
	lifo->list = (void *)0;
	_nano_wait_q_init(&lifo->wait_q);
}

#ifdef CONFIG_MICROKERNEL
/*
 * For legacy reasons, the microkernel utilizes the _Cput() API which
 * is functionally equivalent to nano_fiber_lifo_putC() (which has been renamed
 * nano_fiber_lifo_put() given that, by default, APIs will be a C interface), an
 * an alias will be generated.
 */

FUNC_ALIAS(_lifo_put, _Cput, void);
#endif /* CONFIG_MICROKERNEL */

FUNC_ALIAS(_lifo_put, nano_isr_lifo_put, void);
FUNC_ALIAS(_lifo_put, nano_fiber_lifo_put, void);

/*******************************************************************************
*
* _lifo_put - add an element to the head of a linked list lifo
*
* This routine adds an element to the head of a lifo object; it may be
* called from either a fiber or an ISR context.  A fiber pending on the lifo
* object will be made ready, but will NOT be scheduled to execute.
*
* RETURNS: N/A
*
* INTERNAL
* This function is capable of supporting invocations from both a fiber and an
* ISR context.  However, the nano_isr_lifo_put and nano_fiber_lifo_put aliases
* are created to support any required implementation differences in the future
* without introducing a source code migration issue.
*/

void _lifo_put(
	struct nano_lifo *lifo, /* lifo on which to put */
	void *data				/* data to insert */
	)
{
	tCCS *ccs;
	unsigned int imask;

	imask = irq_lock_inline();
	ccs = _nano_wait_q_remove(&lifo->wait_q);
	if (ccs != (tCCS *)NULL) {
		fiberRtnValueSet(ccs, (unsigned int)data);
	} else {
		*(void **)data = lifo->list;
		lifo->list = data;
	}

	irq_unlock_inline(imask);
}

/*******************************************************************************
*
* nano_task_lifo_put - add an element to the head of a linked list lifo
*
* This routine adds an element to the head of a lifo object; it can be
* called only from a task context.  A fiber pending on the lifo
* object will be made ready, and will be scheduled to execute.
*
* This routine is only callable by a task.
*
* RETURNS: N/A
*/

void nano_task_lifo_put(
	struct nano_lifo *lifo, /* lifo on which to put */
	void *data				/* data to insert */
	)
{
	tCCS *ccs;
	unsigned int imask;

	imask = irq_lock_inline();
	ccs = _nano_wait_q_remove(&lifo->wait_q);
	if (ccs != (tCCS *)NULL) {
		fiberRtnValueSet(ccs, (unsigned int)data);

		/* swap into the newly ready fiber */

		_Swap(imask);
		return;
	} else {
		*(void **)data = lifo->list;
		lifo->list = data;
	}

	irq_unlock_inline(imask);
}

FUNC_ALIAS(_lifo_get, nano_isr_lifo_get, void *);
FUNC_ALIAS(_lifo_get, nano_fiber_lifo_get, void *);
FUNC_ALIAS(_lifo_get, nano_task_lifo_get, void *);

/*******************************************************************************
*
* _lifo_get - remove the first element from a linked list lifo
*
* Remove the first element from the specified nanokernel linked list lifo;
* it may be called from a fiber, task, or ISR context.
*
* If no elements are available, NULL is returned.  The first word in the
* element contains invalid data because that memory location was used to store
* a pointer to the next element in the linked list.
*
* RETURNS: Pointer to first element in the list if available, otherwise NULL
*
* INTERNAL
* This function is capable of supporting invocations from fiber, task, and ISR
* contexts.  However, the nano_isr_lifo_get, nano_task_lifo_get, and
* nano_fiber_lifo_get aliases are created to support any required
* implementation differences in the future without introducing a source code
* migration issue.
*/

void *_lifo_get(
	struct nano_lifo *lifo /* lifo on which to receive */
	)
{
	void *data;
	unsigned int imask;

	imask = irq_lock_inline();

	data = lifo->list;
	if (data != NULL) {
		lifo->list = *(void **)data;
	}

	irq_unlock_inline(imask);

	return data;
}

/*******************************************************************************
*
* nano_fiber_lifo_get_wait - get the first element from a LIFO, wait if empty
*
* Remove the first element from the specified system-level linked list lifo;
* it can only be called from a fiber context.
*
* If no elements are available, the calling fiber will pend until an element
* is put onto the list.
*
* The first word in the element contains invalid data because that memory
* location was used to store a pointer to the next element in the linked list.
*
* RETURNS: Pointer to first element in the list
*
* INTERNAL
* There exists a separate nano_task_lifo_get_wait() implementation since a
* task context cannot pend on a nanokernel object.  Instead, tasks will poll
* the lifo object.
*/

void *nano_fiber_lifo_get_wait(
	struct nano_lifo *lifo /* lifo on which to receive */
	)
{
	void *data;
	unsigned int imask;

	imask = irq_lock_inline();

	if (lifo->list == NULL) {
		_nano_wait_q_put(&lifo->wait_q);
		data = (void *)_Swap(imask);
	} else {
		data = lifo->list;
		lifo->list = *(void **)data;
		irq_unlock_inline(imask);
	}

	return data;
}

/*******************************************************************************
*
* nano_task_lifo_get_wait - get the first element from a lifo, poll if empty
*
* Remove the first element from the specified nanokernel linked list lifo; it
* can only be called from a task context.
*
* If no elements are available, the calling task will poll until an element is
* put onto the list.
*
* The first word in the element contains invalid data because that memory
* location was used to store a pointer to the next element in the linked list.
*
* RETURNS: Pointer to first element in the list
*/

void *nano_task_lifo_get_wait(
	struct nano_lifo *lifo /* lifo on which to interact */
	)
{
	void *data;
	unsigned int imask;

	/* spin until data is put onto the LIFO */

	while (1) {
		imask = irq_lock_inline();

		/*
		 * Predict that the branch will be taken to break out of the loop.
		 * There is little cost to a misprediction since that leads to idle.
		 */

		if (likely(lifo->list != NULL))
			break;

		/* see explanation in nano_stack.c:nano_task_stack_pop_wait() */

		nano_cpu_atomic_idle(imask);
	}

	data = lifo->list;
	lifo->list = *(void **)data;

	irq_unlock_inline(imask);

	return data;
}
