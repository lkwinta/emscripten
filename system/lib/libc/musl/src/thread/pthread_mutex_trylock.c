#include "pthread_impl.h"

int __pthread_mutex_trylock_owner(pthread_mutex_t *m)
{
	int old, own;
	int type = m->_m_type;
	pthread_t self = __pthread_self();
	int tid = self->tid;
	volatile void *next;

	old = m->_m_lock;
	own = old & 0x3fffffff;
	if (own == tid) {
		if ((type&8) && m->_m_count<0) {
			old &= 0x40000000;
			m->_m_count = 0;
			goto success;
		}
		if ((type&3) == PTHREAD_MUTEX_RECURSIVE) {
			if ((unsigned)m->_m_count >= INT_MAX) return EAGAIN;
			m->_m_count++;
			return 0;
		}
	}
	if (own == 0x3fffffff) return ENOTRECOVERABLE;
	if (own || (old && !(type & 4))) return EBUSY;

	if (type & 128) {
		if (!self->robust_list.off) {
			self->robust_list.off = (char*)&m->_m_lock-(char *)&m->_m_next;
#ifndef __EMSCRIPTEN__ // XXX Emscripten does not have a concept of multiple processes or kernel space, so robust mutex lists don't need to register to kernel.
			__syscall(SYS_set_robust_list, &self->robust_list, 3*sizeof(long));
#endif
		}
		if (m->_m_waiters) tid |= 0x80000000;
		self->robust_list.pending = &m->_m_next;
	}
	tid |= old & 0x40000000;

	if (a_cas(&m->_m_lock, old, tid) != old) {
		self->robust_list.pending = 0;
		if ((type&12)==12 && m->_m_waiters) return ENOTRECOVERABLE;
		return EBUSY;
	}

success:
#ifndef __EMSCRIPTEN__
	if ((type&8) && m->_m_waiters) {
		int priv = (type & 128) ^ 128;
		__syscall(SYS_futex, &m->_m_lock, FUTEX_UNLOCK_PI|priv);
		self->robust_list.pending = 0;
		return (type&4) ? ENOTRECOVERABLE : EBUSY;
	}
#endif

#if defined(__EMSCRIPTEN__) || !defined(NDEBUG)
	// We can get here for normal mutexes too, but only in debug builds
	// (where we track ownership purely for debug purposes).
	if ((type & 15) == PTHREAD_MUTEX_NORMAL) return 0;
#endif

	next = self->robust_list.head;
	m->_m_next = next;
	m->_m_prev = &self->robust_list.head;
	if (next != &self->robust_list.head) *(volatile void *volatile *)
		((char *)next - sizeof(void *)) = &m->_m_next;
	self->robust_list.head = &m->_m_next;
	self->robust_list.pending = 0;

	if (old) {
		m->_m_count = 0;
		return EOWNERDEAD;
	}

	return 0;
}

int __pthread_mutex_trylock(pthread_mutex_t *m)
{
#if !defined(__EMSCRIPTEN__) || defined(NDEBUG)
	/* XXX EMSCRIPTEN always take the slow path in debug builds so we can trap rather than deadlock */
	if ((m->_m_type&15) == PTHREAD_MUTEX_NORMAL)
		return a_cas(&m->_m_lock, 0, EBUSY) & EBUSY;
#endif
	return __pthread_mutex_trylock_owner(m);
}

weak_alias(__pthread_mutex_trylock, pthread_mutex_trylock);
