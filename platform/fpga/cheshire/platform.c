/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019 FORTH-ICS/CARV
 *				Panagiotis Peristerakis <perister@ics.forth.gr>
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_platform.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/ipi/aclint_mswi.h>
#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/serial/uart8250.h>
#include <sbi_utils/timer/aclint_mtimer.h>

#define CHESHIRE_UART_ADDR	      0x03002000
#define CHESHIRE_UART_FREQ	      50000000
#define CHESHIRE_UART_BAUDRATE	      115200
#define CHESHIRE_UART_REG_SHIFT	      2
#define CHESHIRE_UART_REG_WIDTH	      4
#define CHESHIRE_PLIC_ADDR	      0x04000000
#define CHESHIRE_PLIC_NUM_SOURCES     20
#define CHESHIRE_HART_COUNT	      1
#define CHESHIRE_CLINT_ADDR	      0x02040000
#define CHESHIRE_ACLINT_MTIMER_FREQ   1000000
#define CHESHIRE_ACLINT_MSWI_ADDR     (CHESHIRE_CLINT_ADDR + 0x0)
#define CHESHIRE_ACLINT_MTIMER_ADDR   (CHESHIRE_CLINT_ADDR + 0xbff8)
#define CHESHIRE_ACLINT_MTIMECMP_ADDR (CHESHIRE_CLINT_ADDR + 0x4000)

#define CHESHIRE_VGA_ADDR             0x03007000
#define CHESHIRE_FB_ADDR              0xA0000000
#define CHESHIRE_FB_HEIGHT            480
#define CHESHIRE_FB_WIDTH             640
#define CHESHIRE_FB_SIZE			  (CHESHIRE_FB_WIDTH * CHESHIRE_FB_HEIGHT * 2)

#define CSRW(csr, val) asm volatile("csrw  " #csr ", %0" ::"r"(val));

#define CSRC(csr, val) asm volatile("csrc " #csr ", %0" ::"r"(val));

#define CSRS(csr, val) asm volatile("csrs " #csr ", %0" ::"r"(val));

#define CSRR(csr, var) ({ asm volatile("csrr %0, " #csr : "=r"(var)); })

#define MASK_COUNTER_ENABLED 0xffffffff // cycle, time, instret, hpmcounter3-8

static struct platform_uart_data uart = {
	CHESHIRE_UART_ADDR,
	CHESHIRE_UART_FREQ,
	CHESHIRE_UART_BAUDRATE,
};

static struct plic_data plic = {
	.addr = CHESHIRE_PLIC_ADDR,
	.num_src = CHESHIRE_PLIC_NUM_SOURCES,
};

static struct aclint_mswi_data mswi = {
	.addr = CHESHIRE_ACLINT_MSWI_ADDR,
	.size = ACLINT_MSWI_SIZE,
	.first_hartid = 0,
	.hart_count = CHESHIRE_HART_COUNT,
};

static struct aclint_mtimer_data mtimer = {
	.mtime_freq = CHESHIRE_ACLINT_MTIMER_FREQ,
	.mtime_addr = CHESHIRE_ACLINT_MTIMER_ADDR,
	.mtime_size = 8,
	.mtimecmp_addr = CHESHIRE_ACLINT_MTIMECMP_ADDR,
	.mtimecmp_size = 16,
	.first_hartid = 0,
	.hart_count = CHESHIRE_HART_COUNT,
	.has_64bit_mmio = FALSE,
};

/*
 * Cheshire platform early initialization.
 */
static int cheshire_early_init(bool cold_boot)
{
	void *fdt;
	struct platform_uart_data uart_data;
	int rc;

	if (!cold_boot)
		return 0;
	fdt = fdt_get_address();

	rc = fdt_parse_uart8250(fdt, &uart_data, "ns16550a");
	if (!rc)
		uart = uart_data;

	return 0;
}

/*
 * Cheshire platform final initialization.
 */
static int cheshire_final_init(bool cold_boot)
{

	// Enable the LLC
    asm volatile (
        "la t0,0x03001000\n"
        "li t1, 0\n"
        "sw t1, 0(t0)\n"    // llc.CFG_SPM_LOW
        "sw t1, 4(t0)\n"    // llc.CFG_SPM_HIGH
        "li t1, 1\n"
        "sw t1, 16(t0)\n"   // llc.CFG_COMMIT
        ::: "t0", "t1", "memory"
    );


    // Write to mcounteren and scounteren to allow U-mode access to perf counters
    CSRW(mcounteren, MASK_COUNTER_ENABLED);
    CSRW(scounteren, MASK_COUNTER_ENABLED);

	// Enable ARCANE delegation of traps

	/**ARCANE exceptions */
	uint64_t exceptions = 0;
	CSRR(medeleg, exceptions);
	exceptions |= (1UL << 25) | (1UL << 26);
	CSRW(medeleg, exceptions);

    // Now we need to map the events we want to see in U-mode.
    // | Counter | Event ID | Description                        |
    // | ------- | -------- | ---------------------------------- |
    // | 3       | 2        | Number of misses in L1 D-Cache     |
    // | 4       | 1        | Number of misses in L1 I-Cache     |
    // | 5       | 5        | Number of Load Accesses            |
    // | 6       | 6        | Number of Store Accesses           |
    // | 7       | 23       | LLC Miss                           |
    // | 8       | 24       | LLC Eviction                       |

    CSRW(mhpmevent3, 2);
    CSRW(mhpmevent4, 1);
    CSRW(mhpmevent5, 5);
    CSRW(mhpmevent6, 6);
    CSRW(mhpmevent7, 23);
    CSRW(mhpmevent8, 24);

    // Dont inhibit any of the counters
    CSRW(mcountinhibit, 0);

	return 0;
}

/*
 * Initialize the cheshire console.
 */
static int cheshire_console_init(void)
{
	return uart8250_init(uart.addr,
			     uart.freq,
			     uart.baud,
			     CHESHIRE_UART_REG_SHIFT,
			     CHESHIRE_UART_REG_WIDTH);
}

static int plic_cheshire_warm_irqchip_init(int m_cntx_id, int s_cntx_id)
{
//  size_t i, ie_words = CHESHIRE_PLIC_NUM_SOURCES / 32 + 1;

	/* By default, enable all IRQs for M-mode of target HART */
//  if (m_cntx_id > -1) {
//  	for (i = 0; i < ie_words; i++)
//  		plic_set_ie(&plic, m_cntx_id, i, 1);
//  }
//  /* Enable all IRQs for S-mode of target HART */
//  if (s_cntx_id > -1) {
//  	for (i = 0; i < ie_words; i++)
//  		plic_set_ie(&plic, s_cntx_id, i, 1);
//  }
//  /* By default, enable M-mode threshold */
//  if (m_cntx_id > -1)
//  	plic_set_thresh(&plic, m_cntx_id, 1);
//  /* By default, disable S-mode threshold */
//  if (s_cntx_id > -1)
//  	plic_set_thresh(&plic, s_cntx_id, 0);

	return plic_warm_irqchip_init(&plic, m_cntx_id, s_cntx_id);
}

/*
 * Initialize the cheshire interrupt controller for current HART.
 */
static int cheshire_irqchip_init(bool cold_boot)
{
	u32 hartid = current_hartid();
	int ret;

	if (cold_boot) {
		ret = plic_cold_irqchip_init(&plic);
		if (ret)
			return ret;
	}
	return plic_cheshire_warm_irqchip_init(2 * hartid, 2 * hartid + 1);
}

/*
 * Initialize IPI for current HART.
 */
static int cheshire_ipi_init(bool cold_boot)
{
	int ret;

	if (cold_boot) {
		ret = aclint_mswi_cold_init(&mswi);
		if (ret)
			return ret;
	}

	return aclint_mswi_warm_init();
}

/*
 * Initialize cheshire timer for current HART.
 */
static int cheshire_timer_init(bool cold_boot)
{
	int ret;

	if (cold_boot) {
		ret = aclint_mtimer_cold_init(&mtimer, NULL);
		if (ret)
			return ret;
	}

	return aclint_mtimer_warm_init();
}

/*
 * Platform descriptor.
 */
const struct sbi_platform_operations platform_ops = {
	.early_init = cheshire_early_init,
	.final_init = cheshire_final_init,
	.console_init = cheshire_console_init,
	.irqchip_init = cheshire_irqchip_init,
	.ipi_init = cheshire_ipi_init,
	.timer_init = cheshire_timer_init,
};

const struct sbi_platform platform = {
	.opensbi_version = OPENSBI_VERSION,
	.platform_version = SBI_PLATFORM_VERSION(0x0, 0x01),
	.name = "CHESHIRE RISC-V",
	.features = SBI_PLATFORM_DEFAULT_FEATURES,
	.hart_count = CHESHIRE_HART_COUNT,
	.hart_stack_size = SBI_PLATFORM_DEFAULT_HART_STACK_SIZE,
	.platform_ops_addr = (unsigned long)&platform_ops
};
