/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

/*
 * 2002-07 Benny Sjostrand benny@hostmobility.com
 */


#include <asm/io.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/asoundef.h>
#include <sound/cs46xx.h>

#include "cs46xx_lib.h"
#include "dsp_spos.h"

static int cs46xx_dsp_async_init (struct snd_cs46xx *chip,
				  struct dsp_scb_descriptor * fg_entry);

static enum wide_opcode wide_opcodes[] = { 
	WIDE_FOR_BEGIN_LOOP,
	WIDE_FOR_BEGIN_LOOP2,
	WIDE_COND_GOTO_ADDR,
	WIDE_COND_GOTO_CALL,
	WIDE_TBEQ_COND_GOTO_ADDR,
	WIDE_TBEQ_COND_CALL_ADDR,
	WIDE_TBEQ_NCOND_GOTO_ADDR,
	WIDE_TBEQ_NCOND_CALL_ADDR,
	WIDE_TBEQ_COND_GOTO1_ADDR,
	WIDE_TBEQ_COND_CALL1_ADDR,
	WIDE_TBEQ_NCOND_GOTOI_ADDR,
	WIDE_TBEQ_NCOND_CALL1_ADDR
};

static int shadow_and_reallocate_code (struct snd_cs46xx * chip, u32 * data, u32 size,
				       u32 overlay_begin_address)
{
	unsigned int i = 0, j, nreallocated = 0;
	u32 hival,loval,address;
	u32 mop_operands,mop_type,wide_op;
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (snd_BUG_ON(size %2))
		return -EINVAL;
  
	while (i < size) {
		loval = data[i++];
		hival = data[i++];

		if (ins->code.offset > 0) {
			mop_operands = (hival >> 6) & 0x03fff;
			mop_type = mop_operands >> 10;
      
			/* check for wide type instruction */
			if (mop_type == 0 &&
			    (mop_operands & WIDE_LADD_INSTR_MASK) == 0 &&
			    (mop_operands & WIDE_INSTR_MASK) != 0) {
				wide_op = loval & 0x7f;
				for (j = 0;j < ARRAY_SIZE(wide_opcodes); ++j) {
					if (wide_opcodes[j] == wide_op) {
						/* need to reallocate instruction */
						address  = (hival & 0x00FFF) << 5;
						address |=  loval >> 15;
            
						snd_printdd("handle_wideop[1]: %05x:%05x addr %04x\n",hival,loval,address);
            
						if ( !(address & 0x8000) ) {
							address += (ins->code.offset / 2) - overlay_begin_address;
						} else {
							snd_printdd("handle_wideop[1]: ROM symbol not reallocated\n");
						}
            
						hival &= 0xFF000;
						loval &= 0x07FFF;
            
						hival |= ( (address >> 5)  & 0x00FFF);
						loval |= ( (address << 15) & 0xF8000);
            
						address  = (hival & 0x00FFF) << 5;
						address |=  loval >> 15;
            
						snd_printdd("handle_wideop:[2] %05x:%05x addr %04x\n",hival,loval,address);            
						nreallocated ++;
					} /* wide_opcodes[j] == wide_op */
				} /* for */
			} /* mod_type == 0 ... */
		} /* ins->code.offset > 0 */

		ins->code.data[ins->code.size++] = loval;
		ins->code.data[ins->code.size++] = hival;
	}

	snd_printdd("dsp_spos: %d instructions reallocated\n",nreallocated);
	return nreallocated;
}

static struct dsp_segment_desc * get_segment_desc (struct dsp_module_desc * module, int seg_type)
{
	int i;
	for (i = 0;i < module->nsegments; ++i) {
		if (module->segments[i].segment_type == seg_type) {
			return (module->segments + i);
		}
	}

	return NULL;
};

static int find_free_symbol_index (struct dsp_spos_instance * ins)
{
	int index = ins->symbol_table.nsymbols,i;

	for (i = ins->symbol_table.highest_frag_index; i < ins->symbol_table.nsymbols; ++i) {
		if (ins->symbol_table.symbols[i].deleted) {
			index = i;
			break;
		}
	}

	return index;
}

static int add_symbols (struct snd_cs46xx * chip, struct dsp_module_desc * module)
{
	int i;
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (module->symbol_table.nsymbols > 0) {
		if (!strcmp(module->symbol_table.symbols[0].symbol_name, "OVERLAYBEGINADDRESS") &&
		    module->symbol_table.symbols[0].symbol_type == SYMBOL_CONSTANT ) {
			module->overlay_begin_address = module->symbol_table.symbols[0].address;
		}
	}

	for (i = 0;i < module->symbol_table.nsymbols; ++i) {
		if (ins->symbol_table.nsymbols == (DSP_MAX_SYMBOLS - 1)) {
			snd_printk(KERN_ERR "dsp_spos: symbol table is full\n");
			return -ENOMEM;
		}


		if (cs46xx_dsp_lookup_symbol(chip,
					     module->symbol_table.symbols[i].symbol_name,
					     module->symbol_table.symbols[i].symbol_type) == NULL) {

			ins->symbol_table.symbols[ins->symbol_table.nsymbols] = module->symbol_table.symbols[i];
			ins->symbol_table.symbols[ins->symbol_table.nsymbols].address += ((ins->code.offset / 2) - module->overlay_begin_address);
			ins->symbol_table.symbols[ins->symbol_table.nsymbols].module = module;
			ins->symbol_table.symbols[ins->symbol_table.nsymbols].deleted = 0;

			if (ins->symbol_table.nsymbols > ins->symbol_table.highest_frag_index) 
				ins->symbol_table.highest_frag_index = ins->symbol_table.nsymbols;

			ins->symbol_table.nsymbols++;
		} else {
          /* if (0) printk ("dsp_spos: symbol <%s> duplicated, probably nothing wrong with that (Cirrus?)\n",
                             module->symbol_table.symbols[i].symbol_name); */
		}
	}

	return 0;
}

static struct dsp_symbol_entry *
add_symbol (struct snd_cs46xx * chip, char * symbol_name, u32 address, int type)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_symbol_entry * symbol = NULL;
	int index;

	if (ins->symbol_table.nsymbols == (DSP_MAX_SYMBOLS - 1)) {
		snd_printk(KERN_ERR "dsp_spos: symbol table is full\n");
		return NULL;
	}
  
	if (cs46xx_dsp_lookup_symbol(chip,
				     symbol_name,
				     type) != NULL) {
		snd_printk(KERN_ERR "dsp_spos: symbol <%s> duplicated\n", symbol_name);
		return NULL;
	}

	index = find_free_symbol_index (ins);

	strcpy (ins->symbol_table.symbols[index].symbol_name, symbol_name);
	ins->symbol_table.symbols[index].address = address;
	ins->symbol_table.symbols[index].symbol_type = type;
	ins->symbol_table.symbols[index].module = NULL;
	ins->symbol_table.symbols[index].deleted = 0;
	symbol = (ins->symbol_table.symbols + index);

	if (index > ins->symbol_table.highest_frag_index) 
		ins->symbol_table.highest_frag_index = index;

	if (index == ins->symbol_table.nsymbols)
		ins->symbol_table.nsymbols++; /* no frag. in list */

	return symbol;
}

struct dsp_spos_instance *cs46xx_dsp_spos_create (struct snd_cs46xx * chip)
{
	struct dsp_spos_instance * ins = kzalloc(sizeof(struct dsp_spos_instance), GFP_KERNEL);

	if (ins == NULL) 
		return NULL;

	/* better to use vmalloc for this big table */
	ins->symbol_table.nsymbols = 0;
	ins->symbol_table.symbols = vmalloc(sizeof(struct dsp_symbol_entry) *
					    DSP_MAX_SYMBOLS);
	ins->symbol_table.highest_frag_index = 0;

	if (ins->symbol_table.symbols == NULL) {
		cs46xx_dsp_spos_destroy(chip);
		goto error;
	}

	ins->code.offset = 0;
	ins->code.size = 0;
	ins->code.data = kmalloc(DSP_CODE_BYTE_SIZE, GFP_KERNEL);

	if (ins->code.data == NULL) {
		cs46xx_dsp_spos_destroy(chip);
		goto error;
	}

	ins->nscb = 0;
	ins->ntask = 0;

	ins->nmodules = 0;
	ins->modules = kmalloc(sizeof(struct dsp_module_desc) * DSP_MAX_MODULES, GFP_KERNEL);

	if (ins->modules == NULL) {
		cs46xx_dsp_spos_destroy(chip);
		goto error;
	}

	/* default SPDIF input sample rate
	   to 48000 khz */
	ins->spdif_in_sample_rate = 48000;

	/* maximize volume */
	ins->dac_volume_right = 0x8000;
	ins->dac_volume_left = 0x8000;
	ins->spdif_input_volume_right = 0x8000;
	ins->spdif_input_volume_left = 0x8000;

	/* set left and right validity bits and
	   default channel status */
	ins->spdif_csuv_default = 
		ins->spdif_csuv_stream =  
	 /* byte 0 */  ((unsigned int)_wrap_all_bits(  (SNDRV_PCM_DEFAULT_CON_SPDIF        & 0xff)) << 24) |
	 /* byte 1 */  ((unsigned int)_wrap_all_bits( ((SNDRV_PCM_DEFAULT_CON_SPDIF >> 8) & 0xff)) << 16) |
	 /* byte 3 */   (unsigned int)_wrap_all_bits(  (SNDRV_PCM_DEFAULT_CON_SPDIF >> 24) & 0xff) |
	 /* left and right validity bits */ (1 << 13) | (1 << 12);

	return ins;

error:
	kfree(ins);
	return NULL;
}

void  cs46xx_dsp_spos_destroy (struct snd_cs46xx * chip)
{
	int i;
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (snd_BUG_ON(!ins))
		return;

	mutex_lock(&chip->spos_mutex);
	for (i = 0; i < ins->nscb; ++i) {
		if (ins->scbs[i].deleted) continue;

		cs46xx_dsp_proc_free_scb_desc ( (ins->scbs + i) );
	}

	kfree(ins->code.data);
	vfree(ins->symbol_table.symbols);
	kfree(ins->modules);
	kfree(ins);
	mutex_unlock(&chip->spos_mutex);
}

static int dsp_load_parameter(struct snd_cs46xx *chip,
			      struct dsp_segment_desc *parameter)
{
	u32 doffset, dsize;

	if (!parameter) {
		snd_printdd("dsp_spos: module got no parameter segment\n");
		return 0;
	}

	doffset = (parameter->offset * 4 + DSP_PARAMETER_BYTE_OFFSET);
	dsize   = parameter->size * 4;

	snd_printdd("dsp_spos: "
		    "downloading parameter data to chip (%08x-%08x)\n",
		    doffset,doffset + dsize);
	if (snd_cs46xx_download (chip, parameter->data, doffset, dsize)) {
		snd_printk(KERN_ERR "dsp_spos: "
			   "failed to download parameter data to DSP\n");
		return -EINVAL;
	}
	return 0;
}

static int dsp_load_sample(struct snd_cs46xx *chip,
			   struct dsp_segment_desc *sample)
{
	u32 doffset, dsize;

	if (!sample) {
		snd_printdd("dsp_spos: module got no sample segment\n");
		return 0;
	}

	doffset = (sample->offset * 4  + DSP_SAMPLE_BYTE_OFFSET);
	dsize   =  sample->size * 4;

	snd_printdd("dsp_spos: downloading sample data to chip (%08x-%08x)\n",
		    doffset,doffset + dsize);

	if (snd_cs46xx_download (chip,sample->data,doffset,dsize)) {
		snd_printk(KERN_ERR "dsp_spos: failed to sample data to DSP\n");
		return -EINVAL;
	}
	return 0;
}

int cs46xx_dsp_load_module (struct snd_cs46xx * chip, struct dsp_module_desc * module)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_segment_desc * code = get_segment_desc (module,SEGTYPE_SP_PROGRAM);
	u32 doffset, dsize;
	int err;

	if (ins->nmodules == DSP_MAX_MODULES - 1) {
		snd_printk(KERN_ERR "dsp_spos: to many modules loaded into DSP\n");
		return -ENOMEM;
	}

	snd_printdd("dsp_spos: loading module %s into DSP\n", module->module_name);
  
	if (ins->nmodules == 0) {
		snd_printdd("dsp_spos: clearing parameter area\n");
		snd_cs46xx_clear_BA1(chip, DSP_PARAMETER_BYTE_OFFSET, DSP_PARAMETER_BYTE_SIZE);
	}
  
	err = dsp_load_parameter(chip, get_segment_desc(module,
							SEGTYPE_SP_PARAMETER));
	if (err < 0)
		return err;

	if (ins->nmodules == 0) {
		snd_printdd("dsp_spos: clearing sample area\n");
		snd_cs46xx_clear_BA1(chip, DSP_SAMPLE_BYTE_OFFSET, DSP_SAMPLE_BYTE_SIZE);
	}

	err = dsp_load_sample(chip, get_segment_desc(module,
						     SEGTYPE_SP_SAMPLE));
	if (err < 0)
		return err;

	if (ins->nmodules == 0) {
		snd_printdd("dsp_spos: clearing code area\n");
		snd_cs46xx_clear_BA1(chip, DSP_CODE_BYTE_OFFSET, DSP_CODE_BYTE_SIZE);
	}

	if (code == NULL) {
		snd_printdd("dsp_spos: module got no code segment\n");
	} else {
		if (ins->code.offset + code->size > DSP_CODE_BYTE_SIZE) {
			snd_printk(KERN_ERR "dsp_spos: no space available in DSP\n");
			return -ENOMEM;
		}

		module->load_address = ins->code.offset;
		module->overlay_begin_address = 0x000;

		/* if module has a code segment it must have
		   symbol table */
		if (snd_BUG_ON(!module->symbol_table.symbols))
			return -ENOMEM;
		if (add_symbols(chip,module)) {
			snd_printk(KERN_ERR "dsp_spos: failed to load symbol table\n");
			return -ENOMEM;
		}
    
		doffset = (code->offset * 4 + ins->code.offset * 4 + DSP_CODE_BYTE_OFFSET);
		dsize   = code->size * 4;
		snd_printdd("dsp_spos: downloading code to chip (%08x-%08x)\n",
			    doffset,doffset + dsize);   

		module->nfixups = shadow_and_reallocate_code(chip,code->data,code->size,module->overlay_begin_address);

		if (snd_cs46xx_download (chip,(ins->code.data + ins->code.offset),doffset,dsize)) {
			snd_printk(KERN_ERR "dsp_spos: failed to download code to DSP\n");
			return -EINVAL;
		}

		ins->code.offset += code->size;
	}

	/* NOTE: module segments and symbol table must be
	   statically allocated. Case that module data is
	   not generated by the ospparser */
	ins->modules[ins->nmodules] = *module;
	ins->nmodules++;

	return 0;
}

struct dsp_symbol_entry *
cs46xx_dsp_lookup_symbol (struct snd_cs46xx * chip, char * symbol_name, int symbol_type)
{
	int i;
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	for ( i = 0; i < ins->symbol_table.nsymbols; ++i ) {

		if (ins->symbol_table.symbols[i].deleted)
			continue;

		if (!strcmp(ins->symbol_table.symbols[i].symbol_name,symbol_name) &&
		    ins->symbol_table.symbols[i].symbol_type == symbol_type) {
			return (ins->symbol_table.symbols + i);
		}
	}

#if 0
	printk ("dsp_spos: symbol <%s> type %02x not found\n",
		symbol_name,symbol_type);
#endif

	return NULL;
}


#ifdef CONFIG_PROC_FS
static struct dsp_symbol_entry *
cs46xx_dsp_lookup_symbol_addr (struct snd_cs46xx * chip, u32 address, int symbol_type)
{
	int i;
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	for ( i = 0; i < ins->symbol_table.nsymbols; ++i ) {

		if (ins->symbol_table.symbols[i].deleted)
			continue;

		if (ins->symbol_table.symbols[i].address == address &&
		    ins->symbol_table.symbols[i].symbol_type == symbol_type) {
			return (ins->symbol_table.symbols + i);
		}
	}


	return NULL;
}


static void cs46xx_dsp_proc_symbol_table_read (struct snd_info_entry *entry,
					       struct snd_info_buffer *buffer)
{
	struct snd_cs46xx *chip = entry->private_data;
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	int i;

	snd_iprintf(buffer, "SYMBOLS:\n");
	for ( i = 0; i < ins->symbol_table.nsymbols; ++i ) {
		char *module_str = "system";

		if (ins->symbol_table.symbols[i].deleted)
			continue;

		if (ins->symbol_table.symbols[i].module != NULL) {
			module_str = ins->symbol_table.symbols[i].module->module_name;
		}

    
		snd_iprintf(buffer, "%04X <%02X> %s [%s]\n",
			    ins->symbol_table.symbols[i].address,
			    ins->symbol_table.symbols[i].symbol_type,
			    ins->symbol_table.symbols[i].symbol_name,
			    module_str);    
	}
}


static void cs46xx_dsp_proc_modules_read (struct snd_info_entry *entry,
					  struct snd_info_buffer *buffer)
{
	struct snd_cs46xx *chip = entry->private_data;
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	int i,j;

	mutex_lock(&chip->spos_mutex);
	snd_iprintf(buffer, "MODULES:\n");
	for ( i = 0; i < ins->nmodules; ++i ) {
		snd_iprintf(buffer, "\n%s:\n", ins->modules[i].module_name);
		snd_iprintf(buffer, "   %d symbols\n", ins->modules[i].symbol_table.nsymbols);
		snd_iprintf(buffer, "   %d fixups\n", ins->modules[i].nfixups);

		for (j = 0; j < ins->modules[i].nsegments; ++ j) {
			struct dsp_segment_desc * desc = (ins->modules[i].segments + j);
			snd_iprintf(buffer, "   segment %02x offset %08x size %08x\n",
				    desc->segment_type,desc->offset, desc->size);
		}
	}
	mutex_unlock(&chip->spos_mutex);
}

static void cs46xx_dsp_proc_task_tree_read (struct snd_info_entry *entry,
					    struct snd_info_buffer *buffer)
{
	struct snd_cs46xx *chip = entry->private_data;
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	int i, j, col;
	void __iomem *dst = chip->region.idx[1].remap_addr + DSP_PARAMETER_BYTE_OFFSET;

	mutex_lock(&chip->spos_mutex);
	snd_iprintf(buffer, "TASK TREES:\n");
	for ( i = 0; i < ins->ntask; ++i) {
		snd_iprintf(buffer,"\n%04x %s:\n",ins->tasks[i].address,ins->tasks[i].task_name);

		for (col = 0,j = 0;j < ins->tasks[i].size; j++,col++) {
			u32 val;
			if (col == 4) {
				snd_iprintf(buffer,"\n");
				col = 0;
			}
			val = readl(dst + (ins->tasks[i].address + j) * sizeof(u32));
			snd_iprintf(buffer,"%08x ",val);
		}
	}

	snd_iprintf(buffer,"\n");  
	mutex_unlock(&chip->spos_mutex);
}

static void cs46xx_dsp_proc_scb_read (struct snd_info_entry *entry,
				      struct snd_info_buffer *buffer)
{
	struct snd_cs46xx *chip = entry->private_data;
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	int i;

	mutex_lock(&chip->spos_mutex);
	snd_iprintf(buffer, "SCB's:\n");
	for ( i = 0; i < ins->nscb; ++i) {
		if (ins->scbs[i].deleted)
			continue;
		snd_iprintf(buffer,"\n%04x %s:\n\n",ins->scbs[i].address,ins->scbs[i].scb_name);

		if (ins->scbs[i].parent_scb_ptr != NULL) {
			snd_iprintf(buffer,"parent [%s:%04x] ", 
				    ins->scbs[i].parent_scb_ptr->scb_name,
				    ins->scbs[i].parent_scb_ptr->address);
		} else snd_iprintf(buffer,"parent [none] ");

		snd_iprintf(buffer,"sub_list_ptr [%s:%04x]\nnext_scb_ptr [%s:%04x]  task_entry [%s:%04x]\n",
			    ins->scbs[i].sub_list_ptr->scb_name,
			    ins->scbs[i].sub_list_ptr->address,
			    ins->scbs[i].next_scb_ptr->scb_name,
			    ins->scbs[i].next_scb_ptr->address,
			    ins->scbs[i].task_entry->symbol_name,
			    ins->scbs[i].task_entry->address);
	}

	snd_iprintf(buffer,"\n");
	mutex_unlock(&chip->spos_mutex);
}

static void cs46xx_dsp_proc_parameter_dump_read (struct snd_info_entry *entry,
						 struct snd_info_buffer *buffer)
{
	struct snd_cs46xx *chip = entry->private_data;
	/*struct dsp_spos_instance * ins = chip->dsp_spos_instance; */
	unsigned int i, col = 0;
	void __iomem *dst = chip->region.idx[1].remap_addr + DSP_PARAMETER_BYTE_OFFSET;
	struct dsp_symbol_entry * symbol; 

	for (i = 0;i < DSP_PARAMETER_BYTE_SIZE; i += sizeof(u32),col ++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}

		if ( (symbol = cs46xx_dsp_lookup_symbol_addr (chip,i / sizeof(u32), SYMBOL_PARAMETER)) != NULL) {
			col = 0;
			snd_iprintf (buffer,"\n%s:\n",symbol->symbol_name);
		}

		if (col == 0) {
			snd_iprintf(buffer, "%04X ", i / (unsigned int)sizeof(u32));
		}

		snd_iprintf(buffer,"%08X ",readl(dst + i));
	}
}

static void cs46xx_dsp_proc_sample_dump_read (struct snd_info_entry *entry,
					      struct snd_info_buffer *buffer)
{
	struct snd_cs46xx *chip = entry->private_data;
	int i,col = 0;
	void __iomem *dst = chip->region.idx[2].remap_addr;

	snd_iprintf(buffer,"PCMREADER:\n");
	for (i = PCM_READER_BUF1;i < PCM_READER_BUF1 + 0x30; i += sizeof(u32),col ++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}

		if (col == 0) {
			snd_iprintf(buffer, "%04X ",i);
		}

		snd_iprintf(buffer,"%08X ",readl(dst + i));
	}

	snd_iprintf(buffer,"\nMIX_SAMPLE_BUF1:\n");

	col = 0;
	for (i = MIX_SAMPLE_BUF1;i < MIX_SAMPLE_BUF1 + 0x40; i += sizeof(u32),col ++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}

		if (col == 0) {
			snd_iprintf(buffer, "%04X ",i);
		}

		snd_iprintf(buffer,"%08X ",readl(dst + i));
	}

	snd_iprintf(buffer,"\nSRC_TASK_SCB1:\n");
	col = 0;
	for (i = 0x2480 ; i < 0x2480 + 0x40 ; i += sizeof(u32),col ++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}
		
		if (col == 0) {
			snd_iprintf(buffer, "%04X ",i);
		}

		snd_iprintf(buffer,"%08X ",readl(dst + i));
	}


	snd_iprintf(buffer,"\nSPDIFO_BUFFER:\n");
	col = 0;
	for (i = SPDIFO_IP_OUTPUT_BUFFER1;i < SPDIFO_IP_OUTPUT_BUFFER1 + 0x30; i += sizeof(u32),col ++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}

		if (col == 0) {
			snd_iprintf(buffer, "%04X ",i);
		}

		snd_iprintf(buffer,"%08X ",readl(dst + i));
	}

	snd_iprintf(buffer,"\n...\n");
	col = 0;

	for (i = SPDIFO_IP_OUTPUT_BUFFER1+0xD0;i < SPDIFO_IP_OUTPUT_BUFFER1 + 0x110; i += sizeof(u32),col ++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}

		if (col == 0) {
			snd_iprintf(buffer, "%04X ",i);
		}

		snd_iprintf(buffer,"%08X ",readl(dst + i));
	}


	snd_iprintf(buffer,"\nOUTPUT_SNOOP:\n");
	col = 0;
	for (i = OUTPUT_SNOOP_BUFFER;i < OUTPUT_SNOOP_BUFFER + 0x40; i += sizeof(u32),col ++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}

		if (col == 0) {
			snd_iprintf(buffer, "%04X ",i);
		}

		snd_iprintf(buffer,"%08X ",readl(dst + i));
	}

	snd_iprintf(buffer,"\nCODEC_INPUT_BUF1: \n");
	col = 0;
	for (i = CODEC_INPUT_BUF1;i < CODEC_INPUT_BUF1 + 0x40; i += sizeof(u32),col ++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}

		if (col == 0) {
			snd_iprintf(buffer, "%04X ",i);
		}

		snd_iprintf(buffer,"%08X ",readl(dst + i));
	}
#if 0
	snd_iprintf(buffer,"\nWRITE_BACK_BUF1: \n");
	col = 0;
	for (i = WRITE_BACK_BUF1;i < WRITE_BACK_BUF1 + 0x40; i += sizeof(u32),col ++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}

		if (col == 0) {
			snd_iprintf(buffer, "%04X ",i);
		}

		snd_iprintf(buffer,"%08X ",readl(dst + i));
	}
#endif

	snd_iprintf(buffer,"\nSPDIFI_IP_OUTPUT_BUFFER1: \n");
	col = 0;
	for (i = SPDIFI_IP_OUTPUT_BUFFER1;i < SPDIFI_IP_OUTPUT_BUFFER1 + 0x80; i += sizeof(u32),col ++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}

		if (col == 0) {
			snd_iprintf(buffer, "%04X ",i);
		}
		
		snd_iprintf(buffer,"%08X ",readl(dst + i));
	}
	snd_iprintf(buffer,"\n");
}

int cs46xx_dsp_proc_init (struct snd_card *card, struct snd_cs46xx *chip)
{
	struct snd_info_entry *entry;
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	int i;

	ins->snd_card = card;

	if ((entry = snd_info_create_card_entry(card, "dsp", card->proc_root)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->mode = S_IFDIR | S_IRUGO | S_IXUGO;
      
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}

	ins->proc_dsp_dir = entry;

	if (!ins->proc_dsp_dir)
		return -ENOMEM;

	if ((entry = snd_info_create_card_entry(card, "spos_symbols", ins->proc_dsp_dir)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = chip;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read = cs46xx_dsp_proc_symbol_table_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ins->proc_sym_info_entry = entry;
    
	if ((entry = snd_info_create_card_entry(card, "spos_modules", ins->proc_dsp_dir)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = chip;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read = cs46xx_dsp_proc_modules_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ins->proc_modules_info_entry = entry;

	if ((entry = snd_info_create_card_entry(card, "parameter", ins->proc_dsp_dir)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = chip;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read = cs46xx_dsp_proc_parameter_dump_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ins->proc_parameter_dump_info_entry = entry;

	if ((entry = snd_info_create_card_entry(card, "sample", ins->proc_dsp_dir)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = chip;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read = cs46xx_dsp_proc_sample_dump_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ins->proc_sample_dump_info_entry = entry;

	if ((entry = snd_info_create_card_entry(card, "task_tree", ins->proc_dsp_dir)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = chip;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read = cs46xx_dsp_proc_task_tree_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ins->proc_task_info_entry = entry;

	if ((entry = snd_info_create_card_entry(card, "scb_info", ins->proc_dsp_dir)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->private_data = chip;
		entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
		entry->c.text.read = cs46xx_dsp_proc_scb_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ins->proc_scb_info_entry = entry;

	mutex_lock(&chip->spos_mutex);
	/* register/update SCB's entries on proc */
	for (i = 0; i < ins->nscb; ++i) {
		if (ins->scbs[i].deleted) continue;

		cs46xx_dsp_proc_register_scb_desc (chip, (ins->scbs + i));
	}
	mutex_unlock(&chip->spos_mutex);

	return 0;
}

int cs46xx_dsp_proc_done (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	int i;

	snd_info_free_entry(ins->proc_sym_info_entry);
	ins->proc_sym_info_entry = NULL;

	snd_info_free_entry(ins->proc_modules_info_entry);
	ins->proc_modules_info_entry = NULL;

	snd_info_free_entry(ins->proc_parameter_dump_info_entry);
	ins->proc_parameter_dump_info_entry = NULL;

	snd_info_free_entry(ins->proc_sample_dump_info_entry);
	ins->proc_sample_dump_info_entry = NULL;

	snd_info_free_entry(ins->proc_scb_info_entry);
	ins->proc_scb_info_entry = NULL;

	snd_info_free_entry(ins->proc_task_info_entry);
	ins->proc_task_info_entry = NULL;

	mutex_lock(&chip->spos_mutex);
	for (i = 0; i < ins->nscb; ++i) {
		if (ins->scbs[i].deleted) continue;
		cs46xx_dsp_proc_free_scb_desc ( (ins->scbs + i) );
	}
	mutex_unlock(&chip->spos_mutex);

	snd_info_free_entry(ins->proc_dsp_dir);
	ins->proc_dsp_dir = NULL;

	return 0;
}
#endif /* CONFIG_PROC_FS */

static int debug_tree;
static void _dsp_create_task_tree (struct snd_cs46xx *chip, u32 * task_data,
				   u32  dest, int size)
{
	void __iomem *spdst = chip->region.idx[1].remap_addr + 
		DSP_PARAMETER_BYTE_OFFSET + dest * sizeof(u32);
	int i;

	for (i = 0; i < size; ++i) {
		if (debug_tree) printk ("addr %p, val %08x\n",spdst,task_data[i]);
		writel(task_data[i],spdst);
		spdst += sizeof(u32);
	}
}

static int debug_scb;
static void _dsp_create_scb (struct snd_cs46xx *chip, u32 * scb_data, u32 dest)
{
	void __iomem *spdst = chip->region.idx[1].remap_addr + 
		DSP_PARAMETER_BYTE_OFFSET + dest * sizeof(u32);
	int i;

	for (i = 0; i < 0x10; ++i) {
		if (debug_scb) printk ("addr %p, val %08x\n",spdst,scb_data[i]);
		writel(scb_data[i],spdst);
		spdst += sizeof(u32);
	}
}

static int find_free_scb_index (struct dsp_spos_instance * ins)
{
	int index = ins->nscb, i;

	for (i = ins->scb_highest_frag_index; i < ins->nscb; ++i) {
		if (ins->scbs[i].deleted) {
			index = i;
			break;
		}
	}

	return index;
}

static struct dsp_scb_descriptor * _map_scb (struct snd_cs46xx *chip, char * name, u32 dest)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_scb_descriptor * desc = NULL;
	int index;

	if (ins->nscb == DSP_MAX_SCB_DESC - 1) {
		snd_printk(KERN_ERR "dsp_spos: got no place for other SCB\n");
		return NULL;
	}

	index = find_free_scb_index (ins);

	strcpy(ins->scbs[index].scb_name, name);
	ins->scbs[index].address = dest;
	ins->scbs[index].index = index;
	ins->scbs[index].proc_info = NULL;
	ins->scbs[index].ref_count = 1;
	ins->scbs[index].deleted = 0;
	spin_lock_init(&ins->scbs[index].lock);

	desc = (ins->scbs + index);
	ins->scbs[index].scb_symbol = add_symbol (chip, name, dest, SYMBOL_PARAMETER);

	if (index > ins->scb_highest_frag_index)
		ins->scb_highest_frag_index = index;

	if (index == ins->nscb)
		ins->nscb++;

	return desc;
}

static struct dsp_task_descriptor *
_map_task_tree (struct snd_cs46xx *chip, char * name, u32 dest, u32 size)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_task_descriptor * desc = NULL;

	if (ins->ntask == DSP_MAX_TASK_DESC - 1) {
		snd_printk(KERN_ERR "dsp_spos: got no place for other TASK\n");
		return NULL;
	}

	if (name)
		strcpy(ins->tasks[ins->ntask].task_name, name);
	else
		strcpy(ins->tasks[ins->ntask].task_name, "(NULL)");
	ins->tasks[ins->ntask].address = dest;
	ins->tasks[ins->ntask].size = size;

	/* quick find in list */
	ins->tasks[ins->ntask].index = ins->ntask;
	desc = (ins->tasks + ins->ntask);
	ins->ntask++;

	if (name)
		add_symbol (chip,name,dest,SYMBOL_PARAMETER);
	return desc;
}

struct dsp_scb_descriptor *
cs46xx_dsp_create_scb (struct snd_cs46xx *chip, char * name, u32 * scb_data, u32 dest)
{
	struct dsp_scb_descriptor * desc;

	desc = _map_scb (chip,name,dest);
	if (desc) {
		desc->data = scb_data;
		_dsp_create_scb(chip,scb_data,dest);
	} else {
		snd_printk(KERN_ERR "dsp_spos: failed to map SCB\n");
	}

	return desc;
}


static struct dsp_task_descriptor *
cs46xx_dsp_create_task_tree (struct snd_cs46xx *chip, char * name, u32 * task_data,
			     u32 dest, int size)
{
	struct dsp_task_descriptor * desc;

	desc = _map_task_tree (chip,name,dest,size);
	if (desc) {
		desc->data = task_data;
		_dsp_create_task_tree(chip,task_data,dest,size);
	} else {
		snd_printk(KERN_ERR "dsp_spos: failed to map TASK\n");
	}

	return desc;
}

int cs46xx_dsp_scb_and_task_init (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_symbol_entry * fg_task_tree_header_code;
	struct dsp_symbol_entry * task_tree_header_code;
	struct dsp_symbol_entry * task_tree_thread;
	struct dsp_symbol_entry * null_algorithm;
	struct dsp_symbol_entry * magic_snoop_task;

	struct dsp_scb_descriptor * timing_master_scb;
	struct dsp_scb_descriptor * codec_out_scb;
	struct dsp_scb_descriptor * codec_in_scb;
	struct dsp_scb_descriptor * src_task_scb;
	struct dsp_scb_descriptor * master_mix_scb;
	struct dsp_scb_descriptor * rear_mix_scb;
	struct dsp_scb_descriptor * record_mix_scb;
	struct dsp_scb_descriptor * write_back_scb;
	struct dsp_scb_descriptor * vari_decimate_scb;
	struct dsp_scb_descriptor * rear_codec_out_scb;
	struct dsp_scb_descriptor * clfe_codec_out_scb;
	struct dsp_scb_descriptor * magic_snoop_scb;
	
	int fifo_addr, fifo_span, valid_slots;

	static struct dsp_spos_control_block sposcb = {
		/* 0 */ HFG_TREE_SCB,HFG_STACK,
		/* 1 */ SPOSCB_ADDR,BG_TREE_SCB_ADDR,
		/* 2 */ DSP_SPOS_DC,0,
		/* 3 */ DSP_SPOS_DC,DSP_SPOS_DC,
		/* 4 */ 0,0,
		/* 5 */ DSP_SPOS_UU,0,
		/* 6 */ FG_TASK_HEADER_ADDR,0,
		/* 7 */ 0,0,
		/* 8 */ DSP_SPOS_UU,DSP_SPOS_DC,
		/* 9 */ 0,
		/* A */ 0,HFG_FIRST_EXECUTE_MODE,
		/* B */ DSP_SPOS_UU,DSP_SPOS_UU,
		/* C */ DSP_SPOS_DC_DC,
		/* D */ DSP_SPOS_DC_DC,
		/* E */ DSP_SPOS_DC_DC,
		/* F */ DSP_SPOS_DC_DC
	};

	cs46xx_dsp_create_task_tree(chip, "sposCB", (u32 *)&sposcb, SPOSCB_ADDR, 0x10);

	null_algorithm  = cs46xx_dsp_lookup_symbol(chip, "NULLALGORITHM", SYMBOL_CODE);
	if (null_algorithm == NULL) {
		snd_printk(KERN_ERR "dsp_spos: symbol NULLALGORITHM not found\n");
		return -EIO;
	}

	fg_task_tree_header_code = cs46xx_dsp_lookup_symbol(chip, "FGTASKTREEHEADERCODE", SYMBOL_CODE);  
	if (fg_task_tree_header_code == NULL) {
		snd_printk(KERN_ERR "dsp_spos: symbol FGTASKTREEHEADERCODE not found\n");
		return -EIO;
	}

	task_tree_header_code = cs46xx_dsp_lookup_symbol(chip, "TASKTREEHEADERCODE", SYMBOL_CODE);  
	if (task_tree_header_code == NULL) {
		snd_printk(KERN_ERR "dsp_spos: symbol TASKTREEHEADERCODE not found\n");
		return -EIO;
	}
  
	task_tree_thread = cs46xx_dsp_lookup_symbol(chip, "TASKTREETHREAD", SYMBOL_CODE);
	if (task_tree_thread == NULL) {
		snd_printk(KERN_ERR "dsp_spos: symbol TASKTREETHREAD not found\n");
		return -EIO;
	}

	magic_snoop_task = cs46xx_dsp_lookup_symbol(chip, "MAGICSNOOPTASK", SYMBOL_CODE);
	if (magic_snoop_task == NULL) {
		snd_printk(KERN_ERR "dsp_spos: symbol MAGICSNOOPTASK not found\n");
		return -EIO;
	}
  
	{
		/* create the null SCB */
		static struct dsp_generic_scb null_scb = {
			{ 0, 0, 0, 0 },
			{ 0, 0, 0, 0, 0 },
			NULL_SCB_ADDR, NULL_SCB_ADDR,
			0, 0, 0, 0, 0,
			{
				0,0,
				0,0,
			}
		};

		null_scb.entry_point = null_algorithm->address;
		ins->the_null_scb = cs46xx_dsp_create_scb(chip, "nullSCB", (u32 *)&null_scb, NULL_SCB_ADDR);
		ins->the_null_scb->task_entry = null_algorithm;
		ins->the_null_scb->sub_list_ptr = ins->the_null_scb;
		ins->the_null_scb->next_scb_ptr = ins->the_null_scb;
		ins->the_null_scb->parent_scb_ptr = NULL;
		cs46xx_dsp_proc_register_scb_desc (chip,ins->the_null_scb);
	}

	{
		/* setup foreground task tree */
		static struct dsp_task_tree_control_block fg_task_tree_hdr =  {
			{ FG_TASK_HEADER_ADDR | (DSP_SPOS_DC << 0x10),
			  DSP_SPOS_DC_DC,
			  DSP_SPOS_DC_DC,
			  0x0000,DSP_SPOS_DC,
			  DSP_SPOS_DC, DSP_SPOS_DC,
			  DSP_SPOS_DC_DC,
			  DSP_SPOS_DC_DC,
			  DSP_SPOS_DC_DC,
			  DSP_SPOS_DC,DSP_SPOS_DC },
    
			{
				BG_TREE_SCB_ADDR,TIMINGMASTER_SCB_ADDR, 
				0,
				FG_TASK_HEADER_ADDR + TCBData,                  
			},

			{    
				4,0,
				1,0,
				2,SPOSCB_ADDR + HFGFlags,
				0,0,
				FG_TASK_HEADER_ADDR + TCBContextBlk,FG_STACK
			},

			{
				DSP_SPOS_DC,0,
				DSP_SPOS_DC,DSP_SPOS_DC,
				DSP_SPOS_DC,DSP_SPOS_DC,
				DSP_SPOS_DC,DSP_SPOS_DC,
				DSP_SPOS_DC,DSP_SPOS_DC,
				DSP_SPOS_DCDC,
				DSP_SPOS_UU,1,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC 
			},                                               
			{ 
				FG_INTERVAL_TIMER_PERIOD,DSP_SPOS_UU,
				0,0
			}
		};

		fg_task_tree_hdr.links.entry_point = fg_task_tree_header_code->address;
		fg_task_tree_hdr.context_blk.stack0 = task_tree_thread->address;
		cs46xx_dsp_create_task_tree(chip,"FGtaskTreeHdr",(u32 *)&fg_task_tree_hdr,FG_TASK_HEADER_ADDR,0x35);
	}


	{
		/* setup foreground task tree */
		static struct dsp_task_tree_control_block bg_task_tree_hdr =  {
			{ DSP_SPOS_DC_DC,
			  DSP_SPOS_DC_DC,
			  DSP_SPOS_DC_DC,
			  DSP_SPOS_DC, DSP_SPOS_DC,
			  DSP_SPOS_DC, DSP_SPOS_DC,
			  DSP_SPOS_DC_DC,
			  DSP_SPOS_DC_DC,
			  DSP_SPOS_DC_DC,
			  DSP_SPOS_DC,DSP_SPOS_DC },
    
			{
				NULL_SCB_ADDR,NULL_SCB_ADDR,  /* Set up the background to do nothing */
				0,
				BG_TREE_SCB_ADDR + TCBData,
			},

			{    
				9999,0,
				0,1,
				0,SPOSCB_ADDR + HFGFlags,
				0,0,
				BG_TREE_SCB_ADDR + TCBContextBlk,BG_STACK
			},

			{
				DSP_SPOS_DC,0,
				DSP_SPOS_DC,DSP_SPOS_DC,
				DSP_SPOS_DC,DSP_SPOS_DC,
				DSP_SPOS_DC,DSP_SPOS_DC,
				DSP_SPOS_DC,DSP_SPOS_DC,
				DSP_SPOS_DCDC,
				DSP_SPOS_UU,1,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC,
				DSP_SPOS_DCDC 
			},                                               
			{ 
				BG_INTERVAL_TIMER_PERIOD,DSP_SPOS_UU,
				0,0
			}
		};

		bg_task_tree_hdr.links.entry_point = task_tree_header_code->address;
		bg_task_tree_hdr.context_blk.stack0 = task_tree_thread->address;
		cs46xx_dsp_create_task_tree(chip,"BGtaskTreeHdr",(u32 *)&bg_task_tree_hdr,BG_TREE_SCB_ADDR,0x35);
	}

	/* create timing master SCB */
	timing_master_scb = cs46xx_dsp_create_timing_master_scb(chip);

	/* create the CODEC output task */
	codec_out_scb = cs46xx_dsp_create_codec_out_scb(chip,"CodecOutSCB_I",0x0010,0x0000,
							MASTERMIX_SCB_ADDR,
							CODECOUT_SCB_ADDR,timing_master_scb,
							SCB_ON_PARENT_SUBLIST_SCB);

	if (!codec_out_scb) goto _fail_end;
	/* create the master mix SCB */
	master_mix_scb = cs46xx_dsp_create_mix_only_scb(chip,"MasterMixSCB",
							MIX_SAMPLE_BUF1,MASTERMIX_SCB_ADDR,
							codec_out_scb,
							SCB_ON_PARENT_SUBLIST_SCB);
	ins->master_mix_scb = master_mix_scb;

	if (!master_mix_scb) goto _fail_end;

	/* create codec in */
	codec_in_scb = cs46xx_dsp_create_codec_in_scb(chip,"CodecInSCB",0x0010,0x00A0,
						      CODEC_INPUT_BUF1,
						      CODECIN_SCB_ADDR,codec_out_scb,
						      SCB_ON_PARENT_NEXT_SCB);
	if (!codec_in_scb) goto _fail_end;
	ins->codec_in_scb = codec_in_scb;

	/* create write back scb */
	write_back_scb = cs46xx_dsp_create_mix_to_ostream_scb(chip,"WriteBackSCB",
							      WRITE_BACK_BUF1,WRITE_BACK_SPB,
							      WRITEBACK_SCB_ADDR,
							      timing_master_scb,
							      SCB_ON_PARENT_NEXT_SCB);
	if (!write_back_scb) goto _fail_end;

	{
		static struct dsp_mix2_ostream_spb mix2_ostream_spb = {
			0x00020000,
			0x0000ffff
		};
    
		if (!cs46xx_dsp_create_task_tree(chip, NULL,
						 (u32 *)&mix2_ostream_spb,
						 WRITE_BACK_SPB, 2))
			goto _fail_end;
	}

	/* input sample converter */
	vari_decimate_scb = cs46xx_dsp_create_vari_decimate_scb(chip,"VariDecimateSCB",
								VARI_DECIMATE_BUF0,
								VARI_DECIMATE_BUF1,
								VARIDECIMATE_SCB_ADDR,
								write_back_scb,
								SCB_ON_PARENT_SUBLIST_SCB);
	if (!vari_decimate_scb) goto _fail_end;

	/* create the record mixer SCB */
	record_mix_scb = cs46xx_dsp_create_mix_only_scb(chip,"RecordMixerSCB",
							MIX_SAMPLE_BUF2,
							RECORD_MIXER_SCB_ADDR,
							vari_decimate_scb,
							SCB_ON_PARENT_SUBLIST_SCB);
	ins->record_mixer_scb = record_mix_scb;

	if (!record_mix_scb) goto _fail_end;

	valid_slots = snd_cs46xx_peekBA0(chip, BA0_ACOSV);

	if (snd_BUG_ON(chip->nr_ac97_codecs != 1 && chip->nr_ac97_codecs != 2))
		goto _fail_end;

	if (chip->nr_ac97_codecs == 1) {
		/* output on slot 5 and 11 
		   on primary CODEC */
		fifo_addr = 0x20;
		fifo_span = 0x60;

		/* enable slot 5 and 11 */
		valid_slots |= ACOSV_SLV5 | ACOSV_SLV11;
	} else {
		/* output on slot 7 and 8 
		   on secondary CODEC */
		fifo_addr = 0x40;
		fifo_span = 0x10;

		/* enable slot 7 and 8 */
		valid_slots |= ACOSV_SLV7 | ACOSV_SLV8;
	}
	/* create CODEC tasklet for rear speakers output*/
	rear_codec_out_scb = cs46xx_dsp_create_codec_out_scb(chip,"CodecOutSCB_Rear",fifo_span,fifo_addr,
							     REAR_MIXER_SCB_ADDR,
							     REAR_CODECOUT_SCB_ADDR,codec_in_scb,
							     SCB_ON_PARENT_NEXT_SCB);
	if (!rear_codec_out_scb) goto _fail_end;
	
	
	/* create the rear PCM channel  mixer SCB */
	rear_mix_scb = cs46xx_dsp_create_mix_only_scb(chip,"RearMixerSCB",
						      MIX_SAMPLE_BUF3,
						      REAR_MIXER_SCB_ADDR,
						      rear_codec_out_scb,
						      SCB_ON_PARENT_SUBLIST_SCB);
	ins->rear_mix_scb = rear_mix_scb;
	if (!rear_mix_scb) goto _fail_end;
	
	if (chip->nr_ac97_codecs == 2) {
		/* create CODEC tasklet for rear Center/LFE output 
		   slot 6 and 9 on seconadry CODEC */
		clfe_codec_out_scb = cs46xx_dsp_create_codec_out_scb(chip,"CodecOutSCB_CLFE",0x0030,0x0030,
								     CLFE_MIXER_SCB_ADDR,
								     CLFE_CODEC_SCB_ADDR,
								     rear_codec_out_scb,
								     SCB_ON_PARENT_NEXT_SCB);
		if (!clfe_codec_out_scb) goto _fail_end;
		
		
		/* create the rear PCM channel  mixer SCB */
		ins->center_lfe_mix_scb = cs46xx_dsp_create_mix_only_scb(chip,"CLFEMixerSCB",
									 MIX_SAMPLE_BUF4,
									 CLFE_MIXER_SCB_ADDR,
									 clfe_codec_out_scb,
									 SCB_ON_PARENT_SUBLIST_SCB);
		if (!ins->center_lfe_mix_scb) goto _fail_end;

		/* enable slot 6 and 9 */
		valid_slots |= ACOSV_SLV6 | ACOSV_SLV9;
	} else {
		clfe_codec_out_scb = rear_codec_out_scb;
		ins->center_lfe_mix_scb = rear_mix_scb;
	}

	/* enable slots depending on CODEC configuration */
	snd_cs46xx_pokeBA0(chip, BA0_ACOSV, valid_slots);

	/* the magic snooper */
	magic_snoop_scb = cs46xx_dsp_create_magic_snoop_scb (chip,"MagicSnoopSCB_I",OUTPUTSNOOP_SCB_ADDR,
							     OUTPUT_SNOOP_BUFFER,
							     codec_out_scb,
							     clfe_codec_out_scb,
							     SCB_ON_PARENT_NEXT_SCB);

    
	if (!magic_snoop_scb) goto _fail_end;
	ins->ref_snoop_scb = magic_snoop_scb;

	/* SP IO access */
	if (!cs46xx_dsp_create_spio_write_scb(chip,"SPIOWriteSCB",SPIOWRITE_SCB_ADDR,
					      magic_snoop_scb,
					      SCB_ON_PARENT_NEXT_SCB))
		goto _fail_end;

	/* SPDIF input sampel rate converter */
	src_task_scb = cs46xx_dsp_create_src_task_scb(chip,"SrcTaskSCB_SPDIFI",
						      ins->spdif_in_sample_rate,
						      SRC_OUTPUT_BUF1,
						      SRC_DELAY_BUF1,SRCTASK_SCB_ADDR,
						      master_mix_scb,
						      SCB_ON_PARENT_SUBLIST_SCB,1);

	if (!src_task_scb) goto _fail_end;
	cs46xx_src_unlink(chip,src_task_scb);

	/* NOTE: when we now how to detect the SPDIF input
	   sample rate we will use this SRC to adjust it */
	ins->spdif_in_src = src_task_scb;

	cs46xx_dsp_async_init(chip,timing_master_scb);
	return 0;

 _fail_end:
	snd_printk(KERN_ERR "dsp_spos: failed to setup SCB's in DSP\n");
	return -EINVAL;
}

static int cs46xx_dsp_async_init (struct snd_cs46xx *chip,
				  struct dsp_scb_descriptor * fg_entry)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_symbol_entry * s16_async_codec_input_task;
	struct dsp_symbol_entry * spdifo_task;
	struct dsp_symbol_entry * spdifi_task;
	struct dsp_scb_descriptor * spdifi_scb_desc, * spdifo_scb_desc, * async_codec_scb_desc;

	s16_async_codec_input_task = cs46xx_dsp_lookup_symbol(chip, "S16_ASYNCCODECINPUTTASK", SYMBOL_CODE);
	if (s16_async_codec_input_task == NULL) {
		snd_printk(KERN_ERR "dsp_spos: symbol S16_ASYNCCODECINPUTTASK not found\n");
		return -EIO;
	}
	spdifo_task = cs46xx_dsp_lookup_symbol(chip, "SPDIFOTASK", SYMBOL_CODE);
	if (spdifo_task == NULL) {
		snd_printk(KERN_ERR "dsp_spos: symbol SPDIFOTASK not found\n");
		return -EIO;
	}

	spdifi_task = cs46xx_dsp_lookup_symbol(chip, "SPDIFITASK", SYMBOL_CODE);
	if (spdifi_task == NULL) {
		snd_printk(KERN_ERR "dsp_spos: symbol SPDIFITASK not found\n");
		return -EIO;
	}

	{
		/* 0xBC0 */
		struct dsp_spdifoscb spdifo_scb = {
			/* 0 */ DSP_SPOS_UUUU,
			{
				/* 1 */ 0xb0, 
				/* 2 */ 0, 
				/* 3 */ 0, 
				/* 4 */ 0, 
			},
			/* NOTE: the SPDIF output task read samples in mono
			   format, the AsynchFGTxSCB task writes to buffer
			   in stereo format
			*/
			/* 5 */ RSCONFIG_SAMPLE_16MONO + RSCONFIG_MODULO_256,
			/* 6 */ ( SPDIFO_IP_OUTPUT_BUFFER1 << 0x10 )  |  0xFFFC,
			/* 7 */ 0,0, 
			/* 8 */ 0, 
			/* 9 */ FG_TASK_HEADER_ADDR, NULL_SCB_ADDR, 
			/* A */ spdifo_task->address,
			SPDIFO_SCB_INST + SPDIFOFIFOPointer,
			{
				/* B */ 0x0040, /*DSP_SPOS_UUUU,*/
				/* C */ 0x20ff, /*DSP_SPOS_UUUU,*/
			},
			/* D */ 0x804c,0,							  /* SPDIFOFIFOPointer:SPDIFOStatRegAddr; */
			/* E */ 0x0108,0x0001,					  /* SPDIFOStMoFormat:SPDIFOFIFOBaseAddr; */
			/* F */ DSP_SPOS_UUUU	  			          /* SPDIFOFree; */
		};

		/* 0xBB0 */
		struct dsp_spdifiscb spdifi_scb = {
			/* 0 */ DSP_SPOS_UULO,DSP_SPOS_UUHI,
			/* 1 */ 0,
			/* 2 */ 0,
			/* 3 */ 1,4000,        /* SPDIFICountLimit SPDIFICount */ 
			/* 4 */ DSP_SPOS_UUUU, /* SPDIFIStatusData */
			/* 5 */ 0,DSP_SPOS_UUHI, /* StatusData, Free4 */
			/* 6 */ DSP_SPOS_UUUU,  /* Free3 */
			/* 7 */ DSP_SPOS_UU,DSP_SPOS_DC,  /* Free2 BitCount*/
			/* 8 */ DSP_SPOS_UUUU,	/* TempStatus */
			/* 9 */ SPDIFO_SCB_INST, NULL_SCB_ADDR,
			/* A */ spdifi_task->address,
			SPDIFI_SCB_INST + SPDIFIFIFOPointer,
			/* NOTE: The SPDIF input task write the sample in mono
			   format from the HW FIFO, the AsynchFGRxSCB task  reads 
			   them in stereo 
			*/
			/* B */ RSCONFIG_SAMPLE_16MONO + RSCONFIG_MODULO_128,
			/* C */ (SPDIFI_IP_OUTPUT_BUFFER1 << 0x10) | 0xFFFC,
			/* D */ 0x8048,0,
			/* E */ 0x01f0,0x0001,
			/* F */ DSP_SPOS_UUUU /* SPDIN_STATUS monitor */
		};

		/* 0xBA0 */
		struct dsp_async_codec_input_scb async_codec_input_scb = {
			/* 0 */ DSP_SPOS_UUUU,
			/* 1 */ 0,
			/* 2 */ 0,
			/* 3 */ 1,4000,
			/* 4 */ 0x0118,0x0001,
			/* 5 */ RSCONFIG_SAMPLE_16MONO + RSCONFIG_MODULO_64,
			/* 6 */ (ASYNC_IP_OUTPUT_BUFFER1 << 0x10) | 0xFFFC,
			/* 7 */ DSP_SPOS_UU,0x3,
			/* 8 */ DSP_SPOS_UUUU,
			/* 9 */ SPDIFI_SCB_INST,NULL_SCB_ADDR,
			/* A */ s16_async_codec_input_task->address,
			HFG_TREE_SCB + AsyncCIOFIFOPointer,
              
			/* B */ RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_64,
			/* C */ (ASYNC_IP_OUTPUT_BUFFER1 << 0x10),  /*(ASYNC_IP_OUTPUT_BUFFER1 << 0x10) | 0xFFFC,*/
      
#ifdef UseASER1Input
			/* short AsyncCIFIFOPointer:AsyncCIStatRegAddr;	       
			   Init. 0000:8042: for ASER1
			   0000:8044: for ASER2 */
			/* D */ 0x8042,0,
      
			/* short AsyncCIStMoFormat:AsyncCIFIFOBaseAddr;
			   Init 1 stero:8050 ASER1
			   Init 0  mono:8070 ASER2
			   Init 1 Stereo : 0100 ASER1 (Set by script) */
			/* E */ 0x0100,0x0001,
      
#endif
      
#ifdef UseASER2Input
			/* short AsyncCIFIFOPointer:AsyncCIStatRegAddr;
			   Init. 0000:8042: for ASER1
			   0000:8044: for ASER2 */
			/* D */ 0x8044,0,
      
			/* short AsyncCIStMoFormat:AsyncCIFIFOBaseAddr;
			   Init 1 stero:8050 ASER1
			   Init 0  mono:8070 ASER2
			   Init 1 Stereo : 0100 ASER1 (Set by script) */
			/* E */ 0x0110,0x0001,
      
#endif
      
			/* short AsyncCIOutputBufModulo:AsyncCIFree;
			   AsyncCIOutputBufModulo: The modulo size for   
			   the output buffer of this task */
			/* F */ 0, /* DSP_SPOS_UUUU */
		};

		spdifo_scb_desc = cs46xx_dsp_create_scb(chip,"SPDIFOSCB",(u32 *)&spdifo_scb,SPDIFO_SCB_INST);

		if (snd_BUG_ON(!spdifo_scb_desc))
			return -EIO;
		spdifi_scb_desc = cs46xx_dsp_create_scb(chip,"SPDIFISCB",(u32 *)&spdifi_scb,SPDIFI_SCB_INST);
		if (snd_BUG_ON(!spdifi_scb_desc))
			return -EIO;
		async_codec_scb_desc = cs46xx_dsp_create_scb(chip,"AsynCodecInputSCB",(u32 *)&async_codec_input_scb, HFG_TREE_SCB);
		if (snd_BUG_ON(!async_codec_scb_desc))
			return -EIO;

		async_codec_scb_desc->parent_scb_ptr = NULL;
		async_codec_scb_desc->next_scb_ptr = spdifi_scb_desc;
		async_codec_scb_desc->sub_list_ptr = ins->the_null_scb;
		async_codec_scb_desc->task_entry = s16_async_codec_input_task;

		spdifi_scb_desc->parent_scb_ptr = async_codec_scb_desc;
		spdifi_scb_desc->next_scb_ptr = spdifo_scb_desc;
		spdifi_scb_desc->sub_list_ptr = ins->the_null_scb;
		spdifi_scb_desc->task_entry = spdifi_task;

		spdifo_scb_desc->parent_scb_ptr = spdifi_scb_desc;
		spdifo_scb_desc->next_scb_ptr = fg_entry;
		spdifo_scb_desc->sub_list_ptr = ins->the_null_scb;
		spdifo_scb_desc->task_entry = spdifo_task;

		/* this one is faked, as the parnet of SPDIFO task
		   is the FG task tree */
		fg_entry->parent_scb_ptr = spdifo_scb_desc;

		/* for proc fs */
		cs46xx_dsp_proc_register_scb_desc (chip,spdifo_scb_desc);
		cs46xx_dsp_proc_register_scb_desc (chip,spdifi_scb_desc);
		cs46xx_dsp_proc_register_scb_desc (chip,async_codec_scb_desc);

		/* Async MASTER ENABLE, affects both SPDIF input and output */
		snd_cs46xx_pokeBA0(chip, BA0_ASER_MASTER, 0x1 );
	}

	return 0;
}

static void cs46xx_dsp_disable_spdif_hw (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	/* set SPDIF output FIFO slot */
	snd_cs46xx_pokeBA0(chip, BA0_ASER_FADDR, 0);

	/* SPDIF output MASTER ENABLE */
	cs46xx_poke_via_dsp (chip,SP_SPDOUT_CONTROL, 0);

	/* right and left validate bit */
	/*cs46xx_poke_via_dsp (chip,SP_SPDOUT_CSUV, ins->spdif_csuv_default);*/
	cs46xx_poke_via_dsp (chip,SP_SPDOUT_CSUV, 0x0);

	/* clear fifo pointer */
	cs46xx_poke_via_dsp (chip,SP_SPDIN_FIFOPTR, 0x0);

	/* monitor state */
	ins->spdif_status_out &= ~DSP_SPDIF_STATUS_HW_ENABLED;
}

int cs46xx_dsp_enable_spdif_hw (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	/* if hw-ctrl already enabled, turn off to reset logic ... */
	cs46xx_dsp_disable_spdif_hw (chip);
	udelay(50);

	/* set SPDIF output FIFO slot */
	snd_cs46xx_pokeBA0(chip, BA0_ASER_FADDR, ( 0x8000 | ((SP_SPDOUT_FIFO >> 4) << 4) ));

	/* SPDIF output MASTER ENABLE */
	cs46xx_poke_via_dsp (chip,SP_SPDOUT_CONTROL, 0x80000000);

	/* right and left validate bit */
	cs46xx_poke_via_dsp (chip,SP_SPDOUT_CSUV, ins->spdif_csuv_default);

	/* monitor state */
	ins->spdif_status_out |= DSP_SPDIF_STATUS_HW_ENABLED;

	return 0;
}

int cs46xx_dsp_enable_spdif_in (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	/* turn on amplifier */
	chip->active_ctrl(chip, 1);
	chip->amplifier_ctrl(chip, 1);

	if (snd_BUG_ON(ins->asynch_rx_scb))
		return -EINVAL;
	if (snd_BUG_ON(!ins->spdif_in_src))
		return -EINVAL;

	mutex_lock(&chip->spos_mutex);

	if ( ! (ins->spdif_status_out & DSP_SPDIF_STATUS_INPUT_CTRL_ENABLED) ) {
		/* time countdown enable */
		cs46xx_poke_via_dsp (chip,SP_ASER_COUNTDOWN, 0x80000005);
		/* NOTE: 80000005 value is just magic. With all values
		   that I've tested this one seem to give the best result.
		   Got no explication why. (Benny) */

		/* SPDIF input MASTER ENABLE */
		cs46xx_poke_via_dsp (chip,SP_SPDIN_CONTROL, 0x800003ff);

		ins->spdif_status_out |= DSP_SPDIF_STATUS_INPUT_CTRL_ENABLED;
	}

	/* create and start the asynchronous receiver SCB */
	ins->asynch_rx_scb = cs46xx_dsp_create_asynch_fg_rx_scb(chip,"AsynchFGRxSCB",
								ASYNCRX_SCB_ADDR,
								SPDIFI_SCB_INST,
								SPDIFI_IP_OUTPUT_BUFFER1,
								ins->spdif_in_src,
								SCB_ON_PARENT_SUBLIST_SCB);

	spin_lock_irq(&chip->reg_lock);

	/* reset SPDIF input sample buffer pointer */
	/*snd_cs46xx_poke (chip, (SPDIFI_SCB_INST + 0x0c) << 2,
	  (SPDIFI_IP_OUTPUT_BUFFER1 << 0x10) | 0xFFFC);*/

	/* reset FIFO ptr */
	/*cs46xx_poke_via_dsp (chip,SP_SPDIN_FIFOPTR, 0x0);*/
	cs46xx_src_link(chip,ins->spdif_in_src);

	/* unmute SRC volume */
	cs46xx_dsp_scb_set_volume (chip,ins->spdif_in_src,0x7fff,0x7fff);

	spin_unlock_irq(&chip->reg_lock);

	/* set SPDIF input sample rate and unmute
	   NOTE: only 48khz support for SPDIF input this time */
	/* cs46xx_dsp_set_src_sample_rate(chip,ins->spdif_in_src,48000); */

	/* monitor state */
	ins->spdif_status_in = 1;
	mutex_unlock(&chip->spos_mutex);

	return 0;
}

int cs46xx_dsp_disable_spdif_in (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (snd_BUG_ON(!ins->asynch_rx_scb))
		return -EINVAL;
	if (snd_BUG_ON(!ins->spdif_in_src))
		return -EINVAL;

	mutex_lock(&chip->spos_mutex);

	/* Remove the asynchronous receiver SCB */
	cs46xx_dsp_remove_scb (chip,ins->asynch_rx_scb);
	ins->asynch_rx_scb = NULL;

	cs46xx_src_unlink(chip,ins->spdif_in_src);

	/* monitor state */
	ins->spdif_status_in = 0;
	mutex_unlock(&chip->spos_mutex);

	/* restore amplifier */
	chip->active_ctrl(chip, -1);
	chip->amplifier_ctrl(chip, -1);

	return 0;
}

int cs46xx_dsp_enable_pcm_capture (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (snd_BUG_ON(ins->pcm_input))
		return -EINVAL;
	if (snd_BUG_ON(!ins->ref_snoop_scb))
		return -EINVAL;

	mutex_lock(&chip->spos_mutex);
	ins->pcm_input = cs46xx_add_record_source(chip,ins->ref_snoop_scb,PCMSERIALIN_PCM_SCB_ADDR,
                                                  "PCMSerialInput_Wave");
	mutex_unlock(&chip->spos_mutex);

	return 0;
}

int cs46xx_dsp_disable_pcm_capture (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (snd_BUG_ON(!ins->pcm_input))
		return -EINVAL;

	mutex_lock(&chip->spos_mutex);
	cs46xx_dsp_remove_scb (chip,ins->pcm_input);
	ins->pcm_input = NULL;
	mutex_unlock(&chip->spos_mutex);

	return 0;
}

int cs46xx_dsp_enable_adc_capture (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (snd_BUG_ON(ins->adc_input))
		return -EINVAL;
	if (snd_BUG_ON(!ins->codec_in_scb))
		return -EINVAL;

	mutex_lock(&chip->spos_mutex);
	ins->adc_input = cs46xx_add_record_source(chip,ins->codec_in_scb,PCMSERIALIN_SCB_ADDR,
						  "PCMSerialInput_ADC");
	mutex_unlock(&chip->spos_mutex);

	return 0;
}

int cs46xx_dsp_disable_adc_capture (struct snd_cs46xx *chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	if (snd_BUG_ON(!ins->adc_input))
		return -EINVAL;

	mutex_lock(&chip->spos_mutex);
	cs46xx_dsp_remove_scb (chip,ins->adc_input);
	ins->adc_input = NULL;
	mutex_unlock(&chip->spos_mutex);

	return 0;
}

int cs46xx_poke_via_dsp (struct snd_cs46xx *chip, u32 address, u32 data)
{
	u32 temp;
	int  i;

	/* santiy check the parameters.  (These numbers are not 100% correct.  They are
	   a rough guess from looking at the controller spec.) */
	if (address < 0x8000 || address >= 0x9000)
		return -EINVAL;
        
	/* initialize the SP_IO_WRITE SCB with the data. */
	temp = ( address << 16 ) | ( address & 0x0000FFFF);   /* offset 0 <-- address2 : address1 */

	snd_cs46xx_poke(chip,( SPIOWRITE_SCB_ADDR      << 2), temp);
	snd_cs46xx_poke(chip,((SPIOWRITE_SCB_ADDR + 1) << 2), data); /* offset 1 <-- data1 */
	snd_cs46xx_poke(chip,((SPIOWRITE_SCB_ADDR + 2) << 2), data); /* offset 1 <-- data2 */
    
	/* Poke this location to tell the task to start */
	snd_cs46xx_poke(chip,((SPIOWRITE_SCB_ADDR + 6) << 2), SPIOWRITE_SCB_ADDR << 0x10);

	/* Verify that the task ran */
	for (i=0; i<25; i++) {
		udelay(125);

		temp =  snd_cs46xx_peek(chip,((SPIOWRITE_SCB_ADDR + 6) << 2));
		if (temp == 0x00000000)
			break;
	}

	if (i == 25) {
		snd_printk(KERN_ERR "dsp_spos: SPIOWriteTask not responding\n");
		return -EBUSY;
	}

	return 0;
}

int cs46xx_dsp_set_dac_volume (struct snd_cs46xx * chip, u16 left, u16 right)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	struct dsp_scb_descriptor * scb; 

	mutex_lock(&chip->spos_mutex);
	
	/* main output */
	scb = ins->master_mix_scb->sub_list_ptr;
	while (scb != ins->the_null_scb) {
		cs46xx_dsp_scb_set_volume (chip,scb,left,right);
		scb = scb->next_scb_ptr;
	}

	/* rear output */
	scb = ins->rear_mix_scb->sub_list_ptr;
	while (scb != ins->the_null_scb) {
		cs46xx_dsp_scb_set_volume (chip,scb,left,right);
		scb = scb->next_scb_ptr;
	}

	ins->dac_volume_left = left;
	ins->dac_volume_right = right;

	mutex_unlock(&chip->spos_mutex);

	return 0;
}

int cs46xx_dsp_set_iec958_volume (struct snd_cs46xx * chip, u16 left, u16 right)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;

	mutex_lock(&chip->spos_mutex);

	if (ins->asynch_rx_scb != NULL)
		cs46xx_dsp_scb_set_volume (chip,ins->asynch_rx_scb,
					   left,right);

	ins->spdif_input_volume_left = left;
	ins->spdif_input_volume_right = right;

	mutex_unlock(&chip->spos_mutex);

	return 0;
}

#ifdef CONFIG_PM
int cs46xx_dsp_resume(struct snd_cs46xx * chip)
{
	struct dsp_spos_instance * ins = chip->dsp_spos_instance;
	int i, err;

	/* clear parameter, sample and code areas */
	snd_cs46xx_clear_BA1(chip, DSP_PARAMETER_BYTE_OFFSET,
			     DSP_PARAMETER_BYTE_SIZE);
	snd_cs46xx_clear_BA1(chip, DSP_SAMPLE_BYTE_OFFSET,
			     DSP_SAMPLE_BYTE_SIZE);
	snd_cs46xx_clear_BA1(chip, DSP_CODE_BYTE_OFFSET, DSP_CODE_BYTE_SIZE);

	for (i = 0; i < ins->nmodules; i++) {
		struct dsp_module_desc *module = &ins->modules[i];
		struct dsp_segment_desc *seg;
		u32 doffset, dsize;

		seg = get_segment_desc(module, SEGTYPE_SP_PARAMETER);
		err = dsp_load_parameter(chip, seg);
		if (err < 0)
			return err;

		seg = get_segment_desc(module, SEGTYPE_SP_SAMPLE);
		err = dsp_load_sample(chip, seg);
		if (err < 0)
			return err;

		seg = get_segment_desc(module, SEGTYPE_SP_PROGRAM);
		if (!seg)
			continue;

		doffset = seg->offset * 4 + module->load_address * 4
			+ DSP_CODE_BYTE_OFFSET;
		dsize   = seg->size * 4;
		err = snd_cs46xx_download(chip,
					  ins->code.data + module->load_address,
					  doffset, dsize);
		if (err < 0)
			return err;
	}

	for (i = 0; i < ins->ntask; i++) {
		struct dsp_task_descriptor *t = &ins->tasks[i];
		_dsp_create_task_tree(chip, t->data, t->address, t->size);
	}

	for (i = 0; i < ins->nscb; i++) {
		struct dsp_scb_descriptor *s = &ins->scbs[i];
		if (s->deleted)
			continue;
		_dsp_create_scb(chip, s->data, s->address);
	}

	return 0;
}
#endif
