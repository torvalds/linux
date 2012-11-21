/*
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __XEN_PUBLIC_HVM_HVM_OP_H__
#define __XEN_PUBLIC_HVM_HVM_OP_H__

/* Get/set subcommands: the second argument of the hypercall is a
 * pointer to a xen_hvm_param struct. */
#define HVMOP_set_param           0
#define HVMOP_get_param           1
struct xen_hvm_param {
    domid_t  domid;    /* IN */
    uint32_t index;    /* IN */
    uint64_t value;    /* IN/OUT */
};
DEFINE_GUEST_HANDLE_STRUCT(xen_hvm_param);

/* Hint from PV drivers for pagetable destruction. */
#define HVMOP_pagetable_dying       9
struct xen_hvm_pagetable_dying {
    /* Domain with a pagetable about to be destroyed. */
    domid_t  domid;
    /* guest physical address of the toplevel pagetable dying */
    aligned_u64 gpa;
};
typedef struct xen_hvm_pagetable_dying xen_hvm_pagetable_dying_t;
DEFINE_GUEST_HANDLE_STRUCT(xen_hvm_pagetable_dying_t);
 
enum hvmmem_type_t {
    HVMMEM_ram_rw,             /* Normal read/write guest RAM */
    HVMMEM_ram_ro,             /* Read-only; writes are discarded */
    HVMMEM_mmio_dm,            /* Reads and write go to the device model */
};

#define HVMOP_get_mem_type    15
/* Return hvmmem_type_t for the specified pfn. */
struct xen_hvm_get_mem_type {
    /* Domain to be queried. */
    domid_t domid;
    /* OUT variable. */
    uint16_t mem_type;
    uint16_t pad[2]; /* align next field on 8-byte boundary */
    /* IN variable. */
    uint64_t pfn;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_hvm_get_mem_type);

#endif /* __XEN_PUBLIC_HVM_HVM_OP_H__ */
