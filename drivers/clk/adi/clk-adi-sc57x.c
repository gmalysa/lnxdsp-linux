// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Clock support for ADI processor
 *
 * (C) Copyright 2022 - Analog Devices, Inc.
 *
 * Written and/or maintained by Timesys Corporation
 *
 * Author: Greg Malysa <greg.malysa@timesys.com>
 * Contact: Nathan Barrett-Morrison <nathan.morrison@timesys.com>
 *
 */

#include <dt-bindings/clock/adi-sc5xx-clock.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/types.h>
#include <linux/spinlock.h>

#include "clk.h"

static DEFINE_SPINLOCK(cdu_lock);

static struct clk *clks[ADSP_SC57X_CLK_END];
static struct clk_onecell_data clk_data;

static const char * const cgu1_in_sels[] = {"sys_clkin0", "sys_clkin1"};
static const char * const sharc0_sels[] = {"cclk0_0", "sysclk_0", "dummy", "dummy"};
static const char * const sharc1_sels[] = {"cclk0_0", "sysclk_0", "dummy", "dummy"};
static const char * const arm_sels[] = {"cclk1_0", "sysclk_0", "dummy", "dummy"};
static const char * const cdu_ddr_sels[] = {"dclk_0", "dclk_1", "dummy", "dummy"};
static const char * const can_sels[] = {"oclk_0", "oclk_1", "dclk_1", "oclk_0_half"};
static const char * const spdif_sels[] = {"oclk_0", "oclk_1", "dclk_1", "dclk_0"};
static const char * const gige_sels[] = {"sclk1_0", "sclk1_1", "cclk0_1", "oclk_0"};
static const char * const sdio_sels[] = {"oclk_0_half", "cclk1_1_half", "cclk1_1", "dclk_1"};

static void __init sc57x_clock_probe(struct device_node *np)
{
	void __iomem *cgu0;
	void __iomem *cgu1;
	void __iomem *cdu;
	int ret;
	int i;

	cgu0 = of_iomap(np, 0);
	if (IS_ERR(cgu0)) {
		pr_err("Unable to remap CGU0 address (resource 0)\n");
		return;
	}

	cgu1 = of_iomap(np, 1);
	if (IS_ERR(cgu1)) {
		pr_err("Unable to remap CGU1 address (resource 1)\n");
		return;
	}

	cdu = of_iomap(np, 2);
	if (IS_ERR(cdu)) {
		pr_err("Unable to remap CDU address (resource 2)\n");
		return;
	}

	// Input clock configuration
	clks[ADSP_SC57X_CLK_DUMMY] = clk_register_fixed_rate(NULL, "dummy", NULL, 0, 0);
	clks[ADSP_SC57X_CLK_SYS_CLKIN0] = of_clk_get_by_name(np, "sys_clkin0");
	clks[ADSP_SC57X_CLK_SYS_CLKIN1] = of_clk_get_by_name(np, "sys_clkin1");
	clks[ADSP_SC57X_CLK_CGU1_IN] = clk_register_mux(NULL, "cgu1_in_sel",
		cgu1_in_sels, 2, CLK_SET_RATE_PARENT, cdu + CDU_CLKINSEL, 0, 1, 0,
		&cdu_lock);

	// CGU configuration and internal clocks
	clks[ADSP_SC57X_CLK_CGU0_PLL_IN] = clk_register_divider(NULL, "cgu0_df",
		"sys_clkin0", CLK_SET_RATE_PARENT, cgu0 + CGU_CTL, 0, 1, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_PLL_IN] = clk_register_divider(NULL, "cgu1_df",
		"cgu1_in_sel", CLK_SET_RATE_PARENT, cgu1 + CGU_CTL, 0, 1, 0, &cdu_lock);

	// VCO output == PLL output
	clks[ADSP_SC57X_CLK_CGU0_PLLCLK] = sc5xx_cgu_pll("cgu0_pllclk", "cgu0_df",
		cgu0 + CGU_CTL, CGU_MSEL_SHIFT, CGU_MSEL_WIDTH, 0, false, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_PLLCLK] = sc5xx_cgu_pll("cgu1_pllclk", "cgu1_df",
		cgu1 + CGU_CTL, CGU_MSEL_SHIFT, CGU_MSEL_WIDTH, 0, false, &cdu_lock);

	// Dividers from pll output
	clks[ADSP_SC57X_CLK_CGU0_CDIV] = cgu_divider("cgu0_cdiv", "cgu0_pllclk",
		cgu0 + CGU_DIV, 0, 5, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU0_SYSCLK] = cgu_divider("sysclk_0", "cgu0_pllclk",
		cgu0 + CGU_DIV, 8, 5, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU0_DDIV] = cgu_divider("cgu0_ddiv", "cgu0_pllclk",
		cgu0 + CGU_DIV, 16, 5, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU0_ODIV] = cgu_divider("cgu0_odiv", "cgu0_pllclk",
		cgu0 + CGU_DIV, 22, 7, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU0_S0SELDIV] = cgu_divider("cgu0_s0seldiv",
		"sysclk_0", cgu0 + CGU_DIV, 5, 3, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU0_S1SELDIV] = cgu_divider("cgu0_s1seldiv",
		"sysclk_0", cgu0 + CGU_DIV, 13, 3, 0, &cdu_lock);

	clks[ADSP_SC57X_CLK_CGU1_CDIV] = cgu_divider("cgu1_cdiv", "cgu1_pllclk",
		cgu1 + CGU_DIV, 0, 5, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_SYSCLK] = cgu_divider("sysclk_1", "cgu1_pllclk",
		cgu1 + CGU_DIV, 8, 5, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_DDIV] = cgu_divider("cgu1_ddiv", "cgu1_pllclk",
		cgu1 + CGU_DIV, 16, 5, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_ODIV] = cgu_divider("cgu1_odiv", "cgu1_pllclk",
		cgu1 + CGU_DIV, 22, 7, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_S0SELDIV] = cgu_divider("cgu1_s0seldiv",
		"sysclk_1", cgu1 + CGU_DIV, 5, 3, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_S1SELDIV] = cgu_divider("cgu1_s1seldiv",
		"sysclk_1", cgu1 + CGU_DIV, 13, 3, 0, &cdu_lock);

	// Gates to enable CGU outputs
	clks[ADSP_SC57X_CLK_CGU0_CCLK0] = cgu_gate("cclk0_0", "cgu0_cdiv",
		cgu0 + CGU_CCBF_DIS, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU0_CCLK1] = cgu_gate("cclk1_0", "cgu0_cdiv",
		cgu1 + CGU_CCBF_DIS, 1, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU0_OCLK] = cgu_gate("oclk_0", "cgu0_odiv",
		cgu0 + CGU_SCBF_DIS, 3, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU0_DCLK] = cgu_gate("dclk_0", "cgu0_ddiv",
		cgu0 + CGU_SCBF_DIS, 2, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU0_SCLK1] = cgu_gate("sclk1_0", "cgu0_s1seldiv",
		cgu0 + CGU_SCBF_DIS, 1, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU0_SCLK0] = cgu_gate("sclk0_0", "cgu0_s0seldiv",
		cgu0 + CGU_SCBF_DIS, 0, &cdu_lock);

	clks[ADSP_SC57X_CLK_CGU1_CCLK0] = cgu_gate("cclk0_1", "cgu1_cdiv",
		cgu1 + CGU_CCBF_DIS, 0, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_CCLK1] = cgu_gate("cclk1_1", "cgu1_cdiv",
		cgu1 + CGU_CCBF_DIS, 1, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_OCLK] = cgu_gate("oclk_1", "cgu1_odiv",
		cgu1 + CGU_SCBF_DIS, 3, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_DCLK] = cgu_gate("dclk_1", "cgu1_ddiv",
		cgu1 + CGU_SCBF_DIS, 2, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_SCLK1] = cgu_gate("sclk1_1", "cgu1_s1seldiv",
		cgu1 + CGU_SCBF_DIS, 1, &cdu_lock);
	clks[ADSP_SC57X_CLK_CGU1_SCLK0] = cgu_gate("sclk0_1", "cgu1_s0seldiv",
		cgu1 + CGU_SCBF_DIS, 0, &cdu_lock);

	// Extra half rate clocks generated in the CDU
	clks[ADSP_SC57X_CLK_OCLK0_HALF] = clk_register_fixed_factor(NULL, "oclk_0_half",
		"oclk_0", CLK_SET_RATE_PARENT, 1, 2);
	clks[ADSP_SC57X_CLK_CCLK1_1_HALF] = clk_register_fixed_factor(NULL, "cclk1_1_half",
		"cclk1_1", CLK_SET_RATE_PARENT, 1, 2);

	// CDU output muxes
	clks[ADSP_SC57X_CLK_SHARC0_SEL] = cdu_mux("sharc0_sel", cdu + CDU_CFG0,
		sharc0_sels, &cdu_lock);
	clks[ADSP_SC57X_CLK_SHARC1_SEL] = cdu_mux("sharc1_sel", cdu + CDU_CFG1,
		sharc1_sels, &cdu_lock);
	clks[ADSP_SC57X_CLK_ARM_SEL] = cdu_mux("arm_sel", cdu + CDU_CFG2,
		arm_sels, &cdu_lock);
	clks[ADSP_SC57X_CLK_CDU_DDR_SEL] = cdu_mux("cdu_ddr_sel", cdu + CDU_CFG3,
		cdu_ddr_sels, &cdu_lock);
	clks[ADSP_SC57X_CLK_CAN_SEL] = cdu_mux("can_sel", cdu + CDU_CFG4,
		can_sels, &cdu_lock);
	clks[ADSP_SC57X_CLK_SPDIF_SEL] = cdu_mux("spdif_sel", cdu + CDU_CFG5,
		spdif_sels, &cdu_lock);
	clks[ADSP_SC57X_CLK_GIGE_SEL] = cdu_mux("gige_sel", cdu + CDU_CFG7,
		gige_sels, &cdu_lock);
	clks[ADSP_SC57X_CLK_SDIO_SEL] = cdu_mux("sdio_sel", cdu + CDU_CFG9, sdio_sels,
		&cdu_lock);

	// CDU output enable gates
	clks[ADSP_SC57X_CLK_SHARC0] = cdu_gate("sharc0", "sharc0_sel",
		cdu + CDU_CFG0, CLK_IS_CRITICAL, &cdu_lock);
	clks[ADSP_SC57X_CLK_SHARC1] = cdu_gate("sharc1", "sharc1_sel",
		cdu + CDU_CFG1, CLK_IS_CRITICAL, &cdu_lock);
	clks[ADSP_SC57X_CLK_ARM] = cdu_gate("arm", "arm_sel", cdu + CDU_CFG2,
		CLK_IS_CRITICAL, &cdu_lock);
	clks[ADSP_SC57X_CLK_CDU_DDR] = cdu_gate("cdu_ddr", "cdu_ddr_sel",
		cdu + CDU_CFG3, CLK_IS_CRITICAL, &cdu_lock);
	clks[ADSP_SC57X_CLK_CAN] = cdu_gate("can", "can_sel", cdu + CDU_CFG4, 0,
		&cdu_lock);
	clks[ADSP_SC57X_CLK_SPDIF] = cdu_gate("spdif", "spdif_sel", cdu + CDU_CFG5,
		0, &cdu_lock);
	clks[ADSP_SC57X_CLK_GIGE] = cdu_gate("gige", "gige_sel", cdu + CDU_CFG7, 0,
		&cdu_lock);
	clks[ADSP_SC57X_CLK_SDIO] = cdu_gate("sdio", "sdio_sel", cdu + CDU_CFG9, 0,
		&cdu_lock);

	ret = cdu_check_clocks(clks, ARRAY_SIZE(clks));
	if (ret)
		goto cleanup;

	clk_data.clks = clks;
	clk_data.clk_num = ARRAY_SIZE(clks);
	ret = of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
	if (ret < 0) {
		pr_err("Failed to register SoC clock information\n");
		goto cleanup;
	}

	return;

cleanup:
	for (i = 0; i < ARRAY_SIZE(clks); i++)
		clk_unregister(clks[i]);
}

CLK_OF_DECLARE(sc57x_clocks, "adi,sc57x-clocks", sc57x_clock_probe);
