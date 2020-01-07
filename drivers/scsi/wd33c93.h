/*
 *    wd33c93.h -  Linux device driver definitions for the
 *                 Commodore Amiga A2091/590 SCSI controller card
 *
 *    IMPORTANT: This file is for version 1.21 - 20/Mar/1996
 *
 * Copyright (c) 1996 John Shifflett, GeoLog Consulting
 *    john@geolog.com
 *    jshiffle@netcom.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef WD33C93_H
#define WD33C93_H


#define uchar unsigned char


/* wd register names */
#define WD_OWN_ID    0x00
#define WD_CONTROL      0x01
#define WD_TIMEOUT_PERIOD  0x02
#define WD_CDB_1     0x03
#define WD_CDB_2     0x04
#define WD_CDB_3     0x05
#define WD_CDB_4     0x06
#define WD_CDB_5     0x07
#define WD_CDB_6     0x08
#define WD_CDB_7     0x09
#define WD_CDB_8     0x0a
#define WD_CDB_9     0x0b
#define WD_CDB_10    0x0c
#define WD_CDB_11    0x0d
#define WD_CDB_12    0x0e
#define WD_TARGET_LUN      0x0f
#define WD_COMMAND_PHASE   0x10
#define WD_SYNCHRONOUS_TRANSFER 0x11
#define WD_TRANSFER_COUNT_MSB 0x12
#define WD_TRANSFER_COUNT  0x13
#define WD_TRANSFER_COUNT_LSB 0x14
#define WD_DESTINATION_ID  0x15
#define WD_SOURCE_ID    0x16
#define WD_SCSI_STATUS     0x17
#define WD_COMMAND      0x18
#define WD_DATA      0x19
#define WD_QUEUE_TAG    0x1a
#define WD_AUXILIARY_STATUS   0x1f

/* WD commands */
#define WD_CMD_RESET    0x00
#define WD_CMD_ABORT    0x01
#define WD_CMD_ASSERT_ATN  0x02
#define WD_CMD_NEGATE_ACK  0x03
#define WD_CMD_DISCONNECT  0x04
#define WD_CMD_RESELECT    0x05
#define WD_CMD_SEL_ATN     0x06
#define WD_CMD_SEL      0x07
#define WD_CMD_SEL_ATN_XFER   0x08
#define WD_CMD_SEL_XFER    0x09
#define WD_CMD_RESEL_RECEIVE  0x0a
#define WD_CMD_RESEL_SEND  0x0b
#define WD_CMD_WAIT_SEL_RECEIVE 0x0c
#define WD_CMD_TRANS_ADDR  0x18
#define WD_CMD_TRANS_INFO  0x20
#define WD_CMD_TRANSFER_PAD   0x21
#define WD_CMD_SBT_MODE    0x80

/* ASR register */
#define ASR_INT         (0x80)
#define ASR_LCI         (0x40)
#define ASR_BSY         (0x20)
#define ASR_CIP         (0x10)
#define ASR_PE          (0x02)
#define ASR_DBR         (0x01)

/* SCSI Bus Phases */
#define PHS_DATA_OUT    0x00
#define PHS_DATA_IN     0x01
#define PHS_COMMAND     0x02
#define PHS_STATUS      0x03
#define PHS_MESS_OUT    0x06
#define PHS_MESS_IN     0x07

/* Command Status Register definitions */

  /* reset state interrupts */
#define CSR_RESET    0x00
#define CSR_RESET_AF    0x01

  /* successful completion interrupts */
#define CSR_RESELECT    0x10
#define CSR_SELECT      0x11
#define CSR_SEL_XFER_DONE  0x16
#define CSR_XFER_DONE      0x18

  /* paused or aborted interrupts */
#define CSR_MSGIN    0x20
#define CSR_SDP         0x21
#define CSR_SEL_ABORT      0x22
#define CSR_RESEL_ABORT    0x25
#define CSR_RESEL_ABORT_AM 0x27
#define CSR_ABORT    0x28

  /* terminated interrupts */
#define CSR_INVALID     0x40
#define CSR_UNEXP_DISC     0x41
#define CSR_TIMEOUT     0x42
#define CSR_PARITY      0x43
#define CSR_PARITY_ATN     0x44
#define CSR_BAD_STATUS     0x45
#define CSR_UNEXP    0x48

  /* service required interrupts */
#define CSR_RESEL    0x80
#define CSR_RESEL_AM    0x81
#define CSR_DISC     0x85
#define CSR_SRV_REQ     0x88

   /* Own ID/CDB Size register */
#define OWNID_EAF    0x08
#define OWNID_EHP    0x10
#define OWNID_RAF    0x20
#define OWNID_FS_8   0x00
#define OWNID_FS_12  0x40
#define OWNID_FS_16  0x80

   /* define these so we don't have to change a2091.c, etc. */
#define WD33C93_FS_8_10  OWNID_FS_8
#define WD33C93_FS_12_15 OWNID_FS_12
#define WD33C93_FS_16_20 OWNID_FS_16

   /* Control register */
#define CTRL_HSP     0x01
#define CTRL_HA      0x02
#define CTRL_IDI     0x04
#define CTRL_EDI     0x08
#define CTRL_HHP     0x10
#define CTRL_POLLED  0x00
#define CTRL_BURST   0x20
#define CTRL_BUS     0x40
#define CTRL_DMA     0x80

   /* Timeout Period register */
#define TIMEOUT_PERIOD_VALUE  20    /* 20 = 200 ms */

   /* Synchronous Transfer Register */
#define STR_FSS      0x80

   /* Destination ID register */
#define DSTID_DPD    0x40
#define DATA_OUT_DIR 0
#define DATA_IN_DIR  1
#define DSTID_SCC    0x80

   /* Source ID register */
#define SRCID_MASK   0x07
#define SRCID_SIV    0x08
#define SRCID_DSP    0x20
#define SRCID_ES     0x40
#define SRCID_ER     0x80

   /* This is what the 3393 chip looks like to us */
typedef struct {
   volatile unsigned char   SASR;
   char                     pad;
   volatile unsigned char   SCMD;
} wd33c93_regs;


typedef int (*dma_setup_t) (Scsi_Cmnd *SCpnt, int dir_in);
typedef void (*dma_stop_t) (struct Scsi_Host *instance, Scsi_Cmnd *SCpnt,
             int status);


#define DEFAULT_SX_PER   500     /* (ns) fairly safe */
#define DEFAULT_SX_OFF   0       /* aka async */

#define OPTIMUM_SX_PER   252     /* (ns) best we can do (mult-of-4) */
#define OPTIMUM_SX_OFF   12      /* size of wd3393 fifo */

struct sx_period {
   unsigned int   period_ns;
   uchar          reg_value;
   };

/* FEF: defines for hostdata->dma_buffer_pool */

#define BUF_CHIP_ALLOCED 0
#define BUF_SCSI_ALLOCED 1

struct WD33C93_hostdata {
    struct Scsi_Host *next;
    wd33c93_regs     *regp;
    uchar            clock_freq;
    uchar            chip;             /* what kind of wd33c93? */
    uchar            microcode;        /* microcode rev */
    int              dma_dir;          /* data transfer dir. */
    dma_setup_t      dma_setup;
    dma_stop_t       dma_stop;
    uchar            *dma_bounce_buffer;
    unsigned int     dma_bounce_len;
    uchar            dma_buffer_pool;  /* FEF: buffer from chip_ram? */
    volatile uchar   busy[8];          /* index = target, bit = lun */
    volatile Scsi_Cmnd *input_Q;       /* commands waiting to be started */
    volatile Scsi_Cmnd *selecting;     /* trying to select this command */
    volatile Scsi_Cmnd *connected;     /* currently connected command */
    volatile Scsi_Cmnd *disconnected_Q;/* commands waiting for reconnect */
    uchar            state;            /* what we are currently doing */
    uchar            dma;              /* current state of DMA (on/off) */
    uchar            level2;           /* extent to which Level-2 commands are used */
    uchar            disconnect;       /* disconnect/reselect policy */
    unsigned int     args;             /* set from command-line argument */
    uchar            incoming_msg[8];  /* filled during message_in phase */
    int              incoming_ptr;     /* mainly used with EXTENDED messages */
    uchar            outgoing_msg[8];  /* send this during next message_out */
    int              outgoing_len;     /* length of outgoing message */
    unsigned int     default_sx_per;   /* default transfer period for SCSI bus */
    uchar            sync_xfer[8];     /* sync_xfer reg settings per target */
    uchar            sync_stat[8];     /* status of sync negotiation per target */
    uchar            no_sync;          /* bitmask: don't do sync on these targets */
#if 0
    uchar            proc;             /* bitmask: what's in proc output */
#endif
    };


/* defines for hostdata->chip */

#define C_WD33C93       0
#define C_WD33C93A      1
#define C_WD33C93B      2
#define C_UNKNOWN_CHIP  100

/* defines for hostdata->state */

#define S_UNCONNECTED         0
#define S_SELECTING           1
#define S_RUNNING_LEVEL2      2
#define S_CONNECTED           3
#define S_PRE_TMP_DISC        4
#define S_PRE_CMP_DISC        5

/* defines for hostdata->dma */

#define D_DMA_OFF          0
#define D_DMA_RUNNING      1

/* defines for hostdata->level2 */
/* NOTE: only the first 3 are implemented so far */

/*  (The first 8 bits are reserved for compatibility. They function
#define L2_SELECT    2  /* start with SEL_ATN_XFER, but never resume it */
#define L2_BASIC     3  /* resume after STATUS ints & RDP messages */
#define L2_DATA      4  /* resume after DATA_IN/OUT ints */
#define L2_MOST      5  /* resume after anything except a RESELECT int */
#define L2_RESELECT  6  /* resume after everything, including RESELECT ints */
#define L2_ALL       7  /* always resume */

/* defines for hostdata->disconnect */

#define DIS_NEVER    0
#define DIS_ADAPTIVE 1
#define DIS_ALWAYS   2

/* defines for hostdata->args */

#define DB_TEST1              1<<0
#define DB_TEST2              1<<1
#define DB_QUEUE_COMMAND      1<<2
#define DB_EXECUTE            1<<3
#define DB_INTR               1<<4
#define DB_TRANSFER           1<<5
#define DB_MASK               0x3f

/* defines for hostdata->sync_stat[] */

#define SS_UNSET     0
#define SS_FIRST     1
#define SS_WAITING   2
#define SS_SET       3

/* defines for hostdata->proc */

#define PR_VERSION   1<<0
#define PR_INFO      1<<1
#define PR_TOTALS    1<<2
#define PR_CONNECTED 1<<3
#define PR_INPUTQ    1<<4
#define PR_DISCQ     1<<5
#define PR_TEST      1<<6
#define PR_STOP      1<<7


void wd33c93_init (struct Scsi_Host *instance, wd33c93_regs *regs,
         dma_setup_t setup, dma_stop_t stop, int clock_freq);
int wd33c93_abort (Scsi_Cmnd *cmd);
int wd33c93_queuecommand (Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *));
void wd33c93_intr (struct Scsi_Host *instance);
int wd33c93_proc_info(char *, char **, off_t, int, int, int);

#if LINUX_VERSION_CODE >= 0x010300
int wd33c93_reset (Scsi_Cmnd *, unsigned int);
#else
int wd33c93_reset (Scsi_Cmnd *);
#endif

#if 0
struct proc_dir_entry proc_scsi_wd33c93;
#endif

#endif /* WD33C93_H */
