/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */
/* mangled further by Bob Manson (manson@santafe.edu) */

#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/debugreg.h>

#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/system.h>

/* This was determined via brute force. */
#define MAGICNUM 496

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which flags the user has access to. */
/* 1 = access 0 = no access */
#define FLAG_MASK 0x00044dd5

/* set's the trap flag. */
#define TRAP_FLAG 0x100

/*
 * this is the number to subtract from the top of the stack. To find
 * the local frame.
 */

/* A mapping between register number and its offset on the kernel stack.
 * You also need to add MAGICNUM to get past the kernel stack frame
 * to the actual saved user info.
 * The first portion is the switch_stack, then comes the pt_regs.
 * 320 is the size of the switch_stack area.
 */

static int map_reg_to_offset[] = {
   320+0,320+8,320+16,320+24,320+32,320+40,320+48,320+56,320+64, /* 0-8 */
   0,8,16,24,32,40,48, /* 9-15 */
   320+184,320+192,320+200, /* 16-18 */
   320+72,320+80,320+88,320+96,320+104,320+112,320+120, /* 19-25 */
   320+128,320+136,320+144,320+176,320+160,-1, /* 26-31*/

   /* fp registers below */
   64,72,80,88,96,104,112,120,128,136,144,152,160,168,176,184,192,
   200,208,216,224,232,240,248,256,264,272,280,288,296,304,312,

   /* 64 = pc */
   320+168
};

static int offset_of_register(int reg_num) {
   if(reg_num<0 || reg_num>64) {
      return -1;
   }
   return map_reg_to_offset[reg_num];
}

static void unset_singlestep(struct task_struct *child) {
}

static void set_singlestep(struct task_struct *child) {
}

/* change a pid into a task struct. */
static inline struct task_struct * get_task(int pid)
{
	int i;

	for (i = 1; i < NR_TASKS; i++) {
		if (task[i] != NULL && (task[i]->pid == pid))
			return task[i];
	}
	return NULL;
}

/*
 * this routine will get a word off of the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 * MAGICNUM is the amount to skip to get to the actual user regs. It
 * was determined by brute force & asking BufElves.
 */   
static inline long get_stack_long(struct task_struct *task, unsigned long offset)
{
	unsigned char *stack;

	stack = (unsigned char *)task->tss.ksp;
	stack += offset+MAGICNUM;
	return (*((long *)stack));
}

/*
 * this routine will put a word on the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline int put_stack_long(struct task_struct *task, unsigned long offset,
	unsigned long data)
{
	unsigned char * stack;

	stack = (unsigned char *) task->tss.ksp;
	stack += offset+MAGICNUM;
	*(unsigned long *) stack = data;
	return 0;
}

/*
 * This routine gets a long from any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 */
static unsigned long get_long(struct vm_area_struct * vma, unsigned long addr)
{
	pgd_t * pgdir;
	pmd_t * pgmiddle;
	pte_t * pgtable;
	unsigned long page;

#ifdef DEBUG
	printk("Getting long at 0x%lx\n",addr);
#endif
repeat:
	pgdir = pgd_offset(vma->vm_task, addr);
	if (pgd_none(*pgdir)) {
		do_no_page(vma, addr, 0);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return 0;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle)) {
		do_no_page(vma, addr, 0);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return 0;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		do_no_page(vma, addr, 0);
		goto repeat;
	}
	page = pte_page(*pgtable);
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (page >= high_memory)
		return 0;
	page += addr & ~PAGE_MASK;
	return *(unsigned long *) page;
}

/*
 * This routine puts a long into any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 *
 * Now keeps R/W state of page so that a text page stays readonly
 * even if a debugger scribbles breakpoints into it.  -M.U-
 */
static void put_long(struct vm_area_struct * vma, unsigned long addr,
	unsigned long data)
{
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
	unsigned long page;

repeat:
	pgdir = pgd_offset(vma->vm_task, addr);
	if (!pgd_present(*pgdir)) {
		do_no_page(vma, addr, 1);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle)) {
		do_no_page(vma, addr, 1);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		do_no_page(vma, addr, 1);
		goto repeat;
	}
	page = pte_page(*pgtable);
	if (!pte_write(*pgtable)) {
		do_wp_page(vma, addr, 1);
		goto repeat;
	}
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (page < high_memory) {
		page += addr & ~PAGE_MASK;
		*(unsigned long *) page = data;
	}
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
/* this should also re-instate whatever read-only mode there was before */
	*pgtable = pte_mkdirty(mk_pte(page, vma->vm_page_prot));
	invalidate();
}

static struct vm_area_struct * find_extend_vma(struct task_struct * tsk, unsigned long addr)
{
	struct vm_area_struct * vma;

	addr &= PAGE_MASK;
	vma = find_vma(tsk,addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		return NULL;
	if (vma->vm_end - addr > tsk->rlim[RLIMIT_STACK].rlim_cur)
		return NULL;
	vma->vm_offset -= vma->vm_start - addr;
	vma->vm_start = addr;
	return vma;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls get_long() to read a long.
 */
static int read_long(struct task_struct * tsk, unsigned long addr,
	unsigned long * result)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

#ifdef DEBUG
	printk("in read_long\n");
#endif
	if (!vma) {
	        printk("Unable to find vma for addr 0x%lx\n",addr);
		return -EIO;
	}
	if ((addr & ~PAGE_MASK) > (PAGE_SIZE-sizeof(long))) {
		unsigned long low,high;
		struct vm_area_struct * vma_high = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		low = get_long(vma, addr & ~(sizeof(long)-1));
		high = get_long(vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 1:
				low >>= 8;
				low |= high << 56;
				break;
			case 2:
				low >>= 16;
				low |= high << 48;
				break;
			case 3:
				low >>= 24;
				low |= high << 40;
				break;
			case 4:
				low >>= 32;
				low |= high << 32;
				break;
			case 5:
				low >>= 40;
				low |= high << 24;
				break;
			case 6:
				low >>= 48;
				low |= high << 16;
				break;
			case 7:
				low >>= 56;
				low |= high << 8;
				break;
		}
		*result = low;
	} else {
	        long l =get_long(vma, addr);

#ifdef DEBUG
		printk("value is 0x%lx\n",l);
#endif
		*result = l;
	}
	return 0;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls put_long() to write a long.
 */
static int write_long(struct task_struct * tsk, unsigned long addr,
	unsigned long data)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_high = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		low = get_long(vma, addr & ~(sizeof(long)-1));
		high = get_long(vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 0: /* shouldn't happen, but safety first */
				low = data;
				break;
			case 1:
				low &= 0x00000000000000ffL;
				low |= data << 8;
				high &= ~0x000000000000ffL;
				high |= data >> 56;
				break;
			case 2:
				low &= 0x000000000000ffffL;
				low |= data << 16;
				high &= ~0x0000000000ffffL;
				high |= data >> 48;
				break;
			case 3:
				low &= 0x0000000000ffffffL;
				low |= data << 24;
				high &= ~0x00000000ffffffL;
				high |= data >> 40;
				break;
		        case 4:
				low &= 0x00000000ffffffffL;
				low |= data << 32;
				high &= ~0x000000ffffffffL;
				high |= data >> 32;
				break;

			case 5:
				low &= 0x000000ffffffffffL;
				low |= data << 40;
				high &= ~0x0000ffffffffffL;
				high |= data >> 24;
				break;
			case 6:
				low &= 0x0000ffffffffffffL;
				low |= data << 48;
				high &= ~0x00ffffffffffffL;
				high |= data >> 16;
				break;
			case 7:
				low &= 0x00ffffffffffffffL;
				low |= data << 56;
				high &= ~0xffffffffffffffL;
				high |= data >> 8;
				break;
		}
		put_long(vma, addr & ~(sizeof(long)-1),low);
		put_long(vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1),high);
	} else
		put_long(vma, addr, data);
	return 0;
}

/* Uh, this does ugly stuff. It stores the specified value in the a3
 * register. entry.S will swap a3 and the returned value from
 * sys_ptrace() before returning to the user.
 */

static inline void set_success(struct pt_regs *regs,long resval) {
   regs->r19=resval;
}

/* This doesn't do diddly, actually--if the value returned from 
 * sys_ptrace() is != 0, it sets things up properly.
 */

static inline void set_failure(struct pt_regs *regs,long errcode) {
   regs->r19=0;
}

asmlinkage long sys_ptrace(long request, long pid, long addr, long data, int a4, int a5, struct pt_regs regs)
{
	struct task_struct *child;
	struct user * dummy;

	dummy = NULL;

#ifdef DEBUG
	printk("request=%ld pid=%ld addr=0x%lx data=0x%lx\n",request,pid,addr,data);
#endif
	set_success(&regs,0);
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
	   if (current->flags & PF_PTRACED) {
	      set_failure(&regs,-EPERM);
	      return -EPERM;
	   }
	   /* set the ptrace bit in the process flags. */
	   current->flags |= PF_PTRACED;
	   return 0;
	}
	if (pid == 1) {
	   set_failure(&regs,-EPERM);
	   return -EPERM;
	}
	if (!(child = get_task(pid))) {
	   set_failure(&regs,-ESRCH);
	   return -ESRCH;
	}
	if (request == PTRACE_ATTACH) {
	   if (child == current) {
	      set_failure(&regs,-EPERM);
	      return -EPERM;
	   }
	   if ((!child->dumpable ||
		(current->uid != child->euid) ||
		(current->uid != child->uid) ||
		(current->gid != child->egid) ||
		(current->gid != child->gid)) && !suser()) {
	      set_failure(&regs,-EPERM);
	      return -EPERM;
	   }
	   /* the same process cannot be attached many times */
	   if (child->flags & PF_PTRACED) {
	      set_failure(&regs,-EPERM);
	      return -EPERM;
	   }
	   child->flags |= PF_PTRACED;
	   if (child->p_pptr != current) {
	      REMOVE_LINKS(child);
	      child->p_pptr = current;
	      SET_LINKS(child);
	   }
	   send_sig(SIGSTOP, child, 1);
	   return 0;
	}
	if (!(child->flags & PF_PTRACED)) {
#ifdef DEBUG
	   printk("child not traced\n");
#endif
	   set_failure(&regs,-ESRCH);
	   return -ESRCH;
	}
	if (child->state != TASK_STOPPED) {
#ifdef DEBUG
	   printk("child process not stopped\n");
#endif
	   if (request != PTRACE_KILL) {
	      set_failure(&regs,-ESRCH);
	      return -ESRCH;
	   }
	}
	if (child->p_pptr != current) {
#ifdef DEBUG
	   printk("child not parent of this process\n");
#endif
	   set_failure(&regs,-ESRCH);
	   return -ESRCH;
	}

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;
			int res;

#ifdef DEBUG
			printk("doing request at addr 0x%lx\n",addr);
#endif
			res = read_long(child, addr, &tmp);
			if (res < 0) {
			   set_failure(&regs,res);
			   return res;
			}
			else {
			   set_success(&regs,tmp);
			   return 0;
			}
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
		        /* We only allow access to registers. */
			unsigned long tmp;

			tmp = 0;  /* Default return condition */
			if(addr==30) {
			   /* stack pointer */
			   tmp=child->tss.usp;
			}
			else {
			   int reg=addr;
			   addr = offset_of_register(addr);
			   if(addr < 0) {
			      set_failure(&regs,-EIO);
			      return -EIO;
			   }
			   tmp = get_stack_long(child, addr);
#ifdef DEBUG
			   printk("%d = reg 0x%lx=tmp\n",reg,tmp);
#endif
			}
			set_success(&regs,tmp);
			return 0;
		}

      /* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA: {
			long res=write_long(child,addr,data);
			if(res) {
			   set_failure(&regs,res);
			}
			return res;
		}

		case PTRACE_POKEUSR: /* write the specified register */
		{
		   long res;
		   addr= offset_of_register(addr);
		   if(addr < 0) {
		      set_failure(&regs,-EIO);
		      return -EIO;
		   }
		   res=put_stack_long(child,addr,data);
		   if(res) {
		      set_failure(&regs,res);
		   }
		   return res;
		}

		case PTRACE_SYSCALL: /* continue and stop at next 
					(return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			if ((unsigned long) data > NSIG) {
			   set_failure(&regs,-EIO);
			   return -EIO;
			}
			if (request == PTRACE_SYSCALL)
				child->flags |= PF_TRACESYS;
			else
				child->flags &= ~PF_TRACESYS;
			child->exit_code = data;
			child->state = TASK_RUNNING;
			unset_singlestep(child);
			set_success(&regs,data);
			return 0;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {
			child->state = TASK_RUNNING;
			child->exit_code = SIGKILL;
			unset_singlestep(child);
			return 0;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			if ((unsigned long) data > NSIG) {
			   set_failure(&regs,-EIO);
			   return -EIO;
			}
			child->flags &= ~PF_TRACESYS;
			set_singlestep(child);
			child->state = TASK_RUNNING;
			child->exit_code = data;
			/* give it a chance to run. */
			return 0;
		}

		case PTRACE_DETACH: { /* detach a process that was attached. */
			if ((unsigned long) data > NSIG) {
			   set_failure(&regs,-EIO);
			   return -EIO;
			}
			child->flags &= ~(PF_PTRACED|PF_TRACESYS);
			child->state = TASK_RUNNING;
			child->exit_code = data;
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			/* make sure the single step bit is not set. */
			unset_singlestep(child);
			return 0;
		}

		default:
		{
		   set_failure(&regs,-EIO);
		   return -EIO;
		}
	}
}

asmlinkage void syscall_trace(void)
{
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
			!= (PF_PTRACED|PF_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	notify_parent(current);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code)
		current->signal |= (1 << (current->exit_code - 1));
	current->exit_code = 0;
}