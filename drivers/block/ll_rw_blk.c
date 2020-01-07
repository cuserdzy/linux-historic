/*
 *  linux/drivers/block/ll_rw_blk.c
 *
 * Copyright (C) 1991, 1992 Linus Torvalds
 * Copyright (C) 1994,      Karl Keyte: Added support for disk statistics
 */

/*
 * This handles all read/write requests to block devices
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/locks.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/io.h>
#include <linux/blk.h>

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 *
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 */
#define NR_REQUEST	64
static struct request all_requests[NR_REQUEST];

/*
 * used to wait on when there are no free requests
 */
struct wait_queue * wait_for_request = NULL;

/* This specifies how many sectors to read ahead on the disk.  */

int read_ahead[MAX_BLKDEV] = {0, };

/* blk_dev_struct is:
 *	*request_fn
 *	*current_request
 */
struct blk_dev_struct blk_dev[MAX_BLKDEV]; /* initialized by blk_dev_init() */

/*
 * blk_size contains the size of all block-devices in units of 1024 byte
 * sectors:
 *
 * blk_size[MAJOR][MINOR]
 *
 * if (!blk_size[MAJOR]) then no minor size checking is done.
 */
int * blk_size[MAX_BLKDEV] = { NULL, NULL, };

/*
 * blksize_size contains the size of all block-devices:
 *
 * blksize_size[MAJOR][MINOR]
 *
 * if (!blksize_size[MAJOR]) then 1024 bytes is assumed.
 */
int * blksize_size[MAX_BLKDEV] = { NULL, NULL, };

/*
 * hardsect_size contains the size of the hardware sector of a device.
 *
 * hardsect_size[MAJOR][MINOR]
 *
 * if (!hardsect_size[MAJOR])
 *		then 512 bytes is assumed.
 * else
 *		sector_size is hardsect_size[MAJOR][MINOR]
 * This is currently set by some scsi device and read by the msdos fs driver
 * This might be a some uses later.
 */
int * hardsect_size[MAX_BLKDEV] = { NULL, NULL, };

/*
 * "plug" the device if there are no outstanding requests: this will
 * force the transfer to start only after we have put all the requests
 * on the list.
 */
static inline void plug_device(struct blk_dev_struct * dev, struct request * plug)
{
	unsigned long flags;

	plug->rq_status = RQ_INACTIVE;
	plug->cmd = -1;
	plug->next = NULL;
	save_flags(flags);
	cli();
	if (!dev->current_request)
		dev->current_request = plug;
	restore_flags(flags);
}

/*
 * remove the plug and let it rip..
 */
static inline void unplug_device(struct blk_dev_struct * dev)
{
	struct request * req;
	unsigned long flags;

	save_flags(flags);
	cli();
	req = dev->current_request;
	if (req && req->rq_status == RQ_INACTIVE && req->cmd == -1) {
		dev->current_request = req->next;
		(dev->request_fn)();
	}
	restore_flags(flags);
}

/*
 * look for a free request in the first N entries.
 * NOTE: interrupts must be disabled on the way in, and will still
 *       be disabled on the way out.
 */
static inline struct request * get_request(int n, kdev_t dev)
{
	static struct request *prev_found = NULL, *prev_limit = NULL;
	register struct request *req, *limit;

	if (n <= 0)
		panic("get_request(%d): impossible!\n", n);

	limit = all_requests + n;
	if (limit != prev_limit) {
		prev_limit = limit;
		prev_found = all_requests;
	}
	req = prev_found;
	for (;;) {
		req = ((req > all_requests) ? req : limit) - 1;
		if (req->rq_status == RQ_INACTIVE)
			break;
		if (req == prev_found)
			return NULL;
	}
	prev_found = req;
	req->rq_status = RQ_ACTIVE;
	req->rq_dev = dev;
	return req;
}

/*
 * wait until a free request in the first N entries is available.
 */
static struct request * __get_request_wait(int n, kdev_t dev)
{
	register struct request *req;
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&wait_for_request, &wait);
	for (;;) {
		unplug_device(MAJOR(dev)+blk_dev);
		current->state = TASK_UNINTERRUPTIBLE;
		cli();
		req = get_request(n, dev);
		sti();
		if (req)
			break;
		schedule();
	}
	remove_wait_queue(&wait_for_request, &wait);
	current->state = TASK_RUNNING;
	return req;
}

static inline struct request * get_request_wait(int n, kdev_t dev)
{
	register struct request *req;

	cli();
	req = get_request(n, dev);
	sti();
	if (req)
		return req;
	return __get_request_wait(n, dev);
}

/* RO fail safe mechanism */

static long ro_bits[MAX_BLKDEV][8];

int is_read_only(kdev_t dev)
{
	int minor,major;

	major = MAJOR(dev);
	minor = MINOR(dev);
	if (major < 0 || major >= MAX_BLKDEV) return 0;
	return ro_bits[major][minor >> 5] & (1 << (minor & 31));
}

void set_device_ro(kdev_t dev,int flag)
{
	int minor,major;

	major = MAJOR(dev);
	minor = MINOR(dev);
	if (major < 0 || major >= MAX_BLKDEV) return;
	if (flag) ro_bits[major][minor >> 5] |= 1 << (minor & 31);
	else ro_bits[major][minor >> 5] &= ~(1 << (minor & 31));
}

static inline void drive_stat_acct(int cmd, unsigned long nr_sectors, short disk_index)
{
	kstat.dk_drive[disk_index]++;
	if (cmd == READ) {
		kstat.dk_drive_rio[disk_index]++;
		kstat.dk_drive_rblk[disk_index] += nr_sectors;
	}
	else if (cmd == WRITE) {
		kstat.dk_drive_wio[disk_index]++;
		kstat.dk_drive_wblk[disk_index] += nr_sectors;
	} else
		printk("drive_stat_acct: cmd not R/W?\n");
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 *
 * By this point, req->cmd is always either READ/WRITE, never READA/WRITEA,
 * which is important for drive_stat_acct() above.
 */
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
	struct request * tmp;
	short		 disk_index;

	switch (MAJOR(req->rq_dev)) {
		case SCSI_DISK_MAJOR:
			disk_index = (MINOR(req->rq_dev) & 0x0070) >> 4;
			if (disk_index < 4)
				drive_stat_acct(req->cmd, req->nr_sectors, disk_index);
			break;
		case IDE0_MAJOR:	/* same as HD_MAJOR */
		case XT_DISK_MAJOR:
			disk_index = (MINOR(req->rq_dev) & 0x0040) >> 6;
			drive_stat_acct(req->cmd, req->nr_sectors, disk_index);
			break;
		case IDE1_MAJOR:
			disk_index = ((MINOR(req->rq_dev) & 0x0040) >> 6) + 2;
			drive_stat_acct(req->cmd, req->nr_sectors, disk_index);
		default:
			break;
	}

	req->next = NULL;
	cli();
	if (req->bh)
		mark_buffer_clean(req->bh);
	if (!(tmp = dev->current_request)) {
		dev->current_request = req;
		(dev->request_fn)();
		sti();
		return;
	}
	for ( ; tmp->next ; tmp = tmp->next) {
		if ((IN_ORDER(tmp,req) ||
		    !IN_ORDER(tmp,tmp->next)) &&
		    IN_ORDER(req,tmp->next))
			break;
	}
	req->next = tmp->next;
	tmp->next = req;

/* for SCSI devices, call request_fn unconditionally */
	if (scsi_major(MAJOR(req->rq_dev)))
		(dev->request_fn)();

	sti();
}

static void make_request(int major,int rw, struct buffer_head * bh)
{
	unsigned int sector, count;
	struct request * req;
	int rw_ahead, max_req;

	count = bh->b_size >> 9;
	sector = bh->b_blocknr * count;
	if (blk_size[major])
		if (blk_size[major][MINOR(bh->b_dev)] < (sector + count)>>1) {
			bh->b_state = 0;
			printk("attempt to access beyond end of device\n");
			return;
		}
	/* Uhhuh.. Nasty dead-lock possible here.. */
	if (buffer_locked(bh))
		return;
	/* Maybe the above fixes it, and maybe it doesn't boot. Life is interesting */
	lock_buffer(bh);

	rw_ahead = 0;	/* normal case; gets changed below for READA/WRITEA */
	switch (rw) {
		case READA:
			rw_ahead = 1;
			rw = READ;	/* drop into READ */
		case READ:
			if (buffer_uptodate(bh)) {
				unlock_buffer(bh); /* Hmmph! Already have it */
				return;
			}
			kstat.pgpgin++;
			max_req = NR_REQUEST;	/* reads take precedence */
			break;
		case WRITEA:
			rw_ahead = 1;
			rw = WRITE;	/* drop into WRITE */
		case WRITE:
			if (!buffer_dirty(bh)) {
				unlock_buffer(bh); /* Hmmph! Nothing to write */
				return;
			}
			/* We don't allow the write-requests to fill up the
			 * queue completely:  we want some room for reads,
			 * as they take precedence. The last third of the
			 * requests are only for reads.
			 */
			kstat.pgpgout++;
			max_req = (NR_REQUEST * 2) / 3;
			break;
		default:
			printk("make_request: bad block dev cmd, must be R/W/RA/WA\n");
			unlock_buffer(bh);
			return;
	}

/* look for a free request. */
	cli();

/* The scsi disk and cdrom drivers completely remove the request
 * from the queue when they start processing an entry.  For this reason
 * it is safe to continue to add links to the top entry for those devices.
 */
	if ((   major == IDE0_MAJOR	/* same as HD_MAJOR */
	     || major == IDE1_MAJOR
	     || major == FLOPPY_MAJOR
	     || major == SCSI_DISK_MAJOR
	     || major == SCSI_CDROM_MAJOR
	     || major == IDE2_MAJOR
	     || major == IDE3_MAJOR)
	    && (req = blk_dev[major].current_request))
	{
		if (major != SCSI_DISK_MAJOR && major != SCSI_CDROM_MAJOR)
			req = req->next;
		while (req) {
			if (req->rq_dev == bh->b_dev &&
			    !req->sem &&
			    req->cmd == rw &&
			    req->sector + req->nr_sectors == sector &&
			    req->nr_sectors < 244)
			{
				req->bhtail->b_reqnext = bh;
				req->bhtail = bh;
				req->nr_sectors += count;
				mark_buffer_clean(bh);
				sti();
				return;
			}

			if (req->rq_dev == bh->b_dev &&
			    !req->sem &&
			    req->cmd == rw &&
			    req->sector - count == sector &&
			    req->nr_sectors < 244)
			{
			    	req->nr_sectors += count;
			    	bh->b_reqnext = req->bh;
			    	req->buffer = bh->b_data;
			    	req->current_nr_sectors = count;
			    	req->sector = sector;
				mark_buffer_clean(bh);
			    	req->bh = bh;
			    	sti();
			    	return;
			}    

			req = req->next;
		}
	}

/* find an unused request. */
	req = get_request(max_req, bh->b_dev);
	sti();

/* if no request available: if rw_ahead, forget it; otherwise try again blocking.. */
	if (!req) {
		if (rw_ahead) {
			unlock_buffer(bh);
			return;
		}
		req = __get_request_wait(max_req, bh->b_dev);
	}

/* fill up the request-info, and add it to the queue */
	req->cmd = rw;
	req->errors = 0;
	req->sector = sector;
	req->nr_sectors = count;
	req->current_nr_sectors = count;
	req->buffer = bh->b_data;
	req->sem = NULL;
	req->bh = bh;
	req->bhtail = bh;
	req->next = NULL;
	add_request(major+blk_dev,req);
}

/*
 * Swap partitions are now read via brw_page.  ll_rw_page is an
 * asynchronous function now --- we must call wait_on_page afterwards
 * if synchronous IO is required.  
 */
void ll_rw_page(int rw, kdev_t dev, unsigned long page, char * buffer)
{
	unsigned int major = MAJOR(dev);
	int block = page;
	
	if (major >= MAX_BLKDEV || !(blk_dev[major].request_fn)) {
		printk("Trying to read nonexistent block-device %s (%ld)\n",
		       kdevname(dev), page);
		return;
	}
	switch (rw) {
		case READ:
			break;
		case WRITE:
			if (is_read_only(dev)) {
				printk("Can't page to read-only device %s\n",
					kdevname(dev));
				return;
			}
			break;
		default:
			panic("ll_rw_page: bad block dev cmd, must be R/W");
	}
	if (mem_map[MAP_NR(buffer)].locked)
		panic ("ll_rw_page: page already locked");
	mem_map[MAP_NR(buffer)].locked = 1;
	brw_page(rw, (unsigned long) buffer, dev, &block, PAGE_SIZE, 0);
}

/* This function can be used to request a number of buffers from a block
   device. Currently the only restriction is that all buffers must belong to
   the same device */

void ll_rw_block(int rw, int nr, struct buffer_head * bh[])
{
	unsigned int major;
	struct request plug;
	int correct_size;
	struct blk_dev_struct * dev;
	int i;

	/* Make sure that the first block contains something reasonable */
	while (!*bh) {
		bh++;
		if (--nr <= 0)
			return;
	};

	dev = NULL;
	if ((major = MAJOR(bh[0]->b_dev)) < MAX_BLKDEV)
		dev = blk_dev + major;
	if (!dev || !dev->request_fn) {
		printk(
	"ll_rw_block: Trying to read nonexistent block-device %s (%ld)\n",
		kdevname(bh[0]->b_dev), bh[0]->b_blocknr);
		goto sorry;
	}

	/* Determine correct block size for this device.  */
	correct_size = BLOCK_SIZE;
	if (blksize_size[major]) {
		i = blksize_size[major][MINOR(bh[0]->b_dev)];
		if (i)
			correct_size = i;
	}

	/* Verify requested block sizes.  */
	for (i = 0; i < nr; i++) {
		if (bh[i] && bh[i]->b_size != correct_size) {
			printk("ll_rw_block: device %s: "
			       "only %d-char blocks implemented (%lu)\n",
			       kdevname(bh[0]->b_dev),
			       correct_size, bh[i]->b_size);
			goto sorry;
		}
	}

	if ((rw == WRITE || rw == WRITEA) && is_read_only(bh[0]->b_dev)) {
		printk("Can't write to read-only device %s\n",
		       kdevname(bh[0]->b_dev));
		goto sorry;
	}

	/* If there are no pending requests for this device, then we insert
	   a dummy request for that device.  This will prevent the request
	   from starting until we have shoved all of the blocks into the
	   queue, and then we let it rip.  */

	if (nr > 1)
		plug_device(dev, &plug);
	for (i = 0; i < nr; i++) {
		if (bh[i]) {
			set_bit(BH_Req, &bh[i]->b_state);
			make_request(major, rw, bh[i]);
		}
	}
	unplug_device(dev);
	return;

      sorry:
	for (i = 0; i < nr; i++) {
		if (bh[i]) {
			clear_bit(BH_Dirty, &bh[i]->b_state);
			clear_bit(BH_Uptodate, &bh[i]->b_state);
		}
	}
	return;
}

void ll_rw_swap_file(int rw, kdev_t dev, unsigned int *b, int nb, char *buf)
{
	int i, j;
	int buffersize;
	struct request * req[8];
	unsigned int major = MAJOR(dev);
	struct semaphore sem = MUTEX_LOCKED;

	if (major >= MAX_BLKDEV || !(blk_dev[major].request_fn)) {
		printk("ll_rw_swap_file: trying to swap nonexistent block-device\n");
		return;
	}
	switch (rw) {
		case READ:
			break;
		case WRITE:
			if (is_read_only(dev)) {
				printk("Can't swap to read-only device %s\n",
					kdevname(dev));
				return;
			}
			break;
		default:
			panic("ll_rw_swap: bad block dev cmd, must be R/W");
	}
	buffersize = PAGE_SIZE / nb;

	for (j=0, i=0; i<nb;)
	{
		for (; j < 8 && i < nb; j++, i++, buf += buffersize)
		{
			if (j == 0) {
				req[j] = get_request_wait(NR_REQUEST, dev);
			} else {
				cli();
				req[j] = get_request(NR_REQUEST, dev);
				sti();
				if (req[j] == NULL)
					break;
			}
			req[j]->cmd = rw;
			req[j]->errors = 0;
			req[j]->sector = (b[i] * buffersize) >> 9;
			req[j]->nr_sectors = buffersize >> 9;
			req[j]->current_nr_sectors = buffersize >> 9;
			req[j]->buffer = buf;
			req[j]->sem = &sem;
			req[j]->bh = NULL;
			req[j]->next = NULL;
			add_request(major+blk_dev,req[j]);
		}
		while (j > 0) {
			j--;
			down(&sem);
		}
	}
}

int blk_dev_init(void)
{
	struct request * req;
	struct blk_dev_struct *dev;

	for (dev = blk_dev + MAX_BLKDEV; dev-- != blk_dev;) {
		dev->request_fn      = NULL;
		dev->current_request = NULL;
	}

	req = all_requests + NR_REQUEST;
	while (--req >= all_requests) {
		req->rq_status = RQ_INACTIVE;
		req->next = NULL;
	}
	memset(ro_bits,0,sizeof(ro_bits));
#ifdef CONFIG_BLK_DEV_RAM
	rd_init();
#endif
#ifdef CONFIG_BLK_DEV_IDE
	ide_init();		/* this MUST preceed hd_init */
#endif
#ifdef CONFIG_BLK_DEV_HD
	hd_init();
#endif
#ifdef CONFIG_BLK_DEV_XD
	xd_init();
#endif
#ifdef CONFIG_BLK_DEV_FD
	floppy_init();
#else
	outb_p(0xc, 0x3f2);
#endif
#ifdef CONFIG_CDI_INIT
	cdi_init();
#endif CONFIG_CDI_INIT
#ifdef CONFIG_CDU31A
	cdu31a_init();
#endif CONFIG_CDU31A
#ifdef CONFIG_MCD
	mcd_init();
#endif CONFIG_MCD
#ifdef CONFIG_MCDX
	mcdx_init();
#endif CONFIG_MCDX
#ifdef CONFIG_SBPCD
	sbpcd_init();
#endif CONFIG_SBPCD
#ifdef CONFIG_AZTCD
        aztcd_init();
#endif CONFIG_AZTCD
#ifdef CONFIG_CDU535
	sony535_init();
#endif CONFIG_CDU535
#ifdef CONFIG_GSCD
	gscd_init();
#endif CONFIG_GSCD
#ifdef CONFIG_CM206
	cm206_init();
#endif
#ifdef CONFIG_OPTCD
	optcd_init();
#endif CONFIG_OPTCD
#ifdef CONFIG_SJCD
	sjcd_init();
#endif CONFIG_SJCD
	return 0;
}
