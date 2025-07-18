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
	// void *fdt;

	// if (!cold_boot)
	// 	return 0;

	// fdt = fdt_get_address();
	// fdt_fixups(fdt);

	// // Generate test pattern for screen
	// uint16_t RGB[8] = {
	// 	0xffff, //White
	// 	0xffe0, //Yellow
	// 	0x07ff, //Cyan
	// 	0x07E0, //Green
	// 	0xf81f, //Magenta
	// 	0xF800, //Red
	// 	0x001F, //Blue
	// 	0x0000, //Black
	// };
	// int col_width = CHESHIRE_FB_WIDTH / 8;

    // volatile uint16_t *fb = (volatile uint16_t*)(void*)(uintptr_t) CHESHIRE_FB_ADDR;

    // for (int i=0; i < CHESHIRE_FB_HEIGHT; i++) {
    //     for (int j=0; j < CHESHIRE_FB_WIDTH; j++) {
    //         fb[CHESHIRE_FB_WIDTH * i + j] = RGB[j / col_width];
    //     }
    // }

	// // Pointer array to acces VGA control registers.
	// // Every index step increases the pointer by 32bit
	// volatile uint32_t *vga = (volatile uint32_t*)(void*)(uintptr_t) CHESHIRE_VGA_ADDR;

    // // Initialize VGA controller and populate framebuffer
    // // Clk div
    // vga[1] = 0x2;        // 8 for Sim, 2 for FPGA
    
    // // Hori: Visible, Front porch, Sync, Back porch
    // vga[2] = 0x280;
    // vga[3] = 0x10;
    // vga[4] = 0x60;
    // vga[5] = 0x30;

    // // Vert: Visible, Front porch, Sync, Back porch
    // vga[6] = 0x1e0;
    // vga[7] = 0xA;
    // vga[8] = 0x2;
    // vga[9] = 0x21;

    // // Framebuffer start address
    // vga[10] = CHESHIRE_FB_ADDR;     // Low 32 bit
    // vga[11] = 0x0;            // High 32 bit

    // // Framebuffer size
    // vga[12] = CHESHIRE_FB_WIDTH*CHESHIRE_FB_HEIGHT*2;      // 640*480 pixel a 2 byte/pixel

    // // Burst length
    // vga[13] = 16;           // 64b * 16 = 128B Bursts

    // // 0: Enable
    // // 1: Hsync polarity (Active Low  = 0)
    // // 2: Vsync polarity (Active Low  = 0)
    // vga[0] = 0x1;    

	// return 0;
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
