/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <clocksource/arm_arch_timer.h>
#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/kvm_hyp.h>

void __hyp_text __kvm_timer_set_cntvoff(u32 cntvoff_low, u32 cntvoff_high)
{
	u64 cntvoff = (u64)cntvoff_high << 32 | cntvoff_low;
	write_sysreg(cntvoff, cntvoff_el2);
}

void __hyp_text enable_el1_phys_timer_access(void)
{
	u64 val;

	/* Allow physical timer/counter access for the host */
	val = read_sysreg(cnthctl_el2);
	val |= CNTHCTL_EL1PCTEN | CNTHCTL_EL1PCEN;
	write_sysreg(val, cnthctl_el2);
}

void __hyp_text disable_el1_phys_timer_access(void)
{
	u64 val;

	/*
	 * Disallow physical timer access for the guest
	 * Physical counter access is allowed
	 */
	val = read_sysreg(cnthctl_el2);
	val &= ~CNTHCTL_EL1PCEN;
	val |= CNTHCTL_EL1PCTEN;
	write_sysreg(val, cnthctl_el2);
}

void __hyp_text __timer_disable_traps(struct kvm_vcpu *vcpu)
{
	/*
	 * We don't need to do this for VHE since the host kernel runs in EL2
	 * with HCR_EL2.TGE ==1, which makes those bits have no impact.
	 */
	if (!has_vhe())
		enable_el1_phys_timer_access();
}

void __hyp_text __timer_enable_traps(struct kvm_vcpu *vcpu)
{
	if (!has_vhe())
		disable_el1_phys_timer_access();
}
