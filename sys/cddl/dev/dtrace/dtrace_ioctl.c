/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 *
 */

static int dtrace_verbose_ioctl;
SYSCTL_INT(_debug_dtrace, OID_AUTO, verbose_ioctl, CTLFLAG_RW,
    &dtrace_verbose_ioctl, 0, "log DTrace ioctls");

#define DTRACE_IOCTL_PRINTF(fmt, ...)	if (dtrace_verbose_ioctl) printf(fmt, ## __VA_ARGS__ )

static int
dtrace_ioctl_helper(struct cdev *dev, u_long cmd, caddr_t addr, int flags,
    struct thread *td)
{
	struct proc *p;
	dof_helper_t *dhp;
	dof_hdr_t *dof;
	int rval;

	dhp = NULL;
	dof = NULL;
	rval = 0;
	switch (cmd) {
	case DTRACEHIOC_ADDDOF:
		dhp = (dof_helper_t *)addr;
		addr = (caddr_t)(uintptr_t)dhp->dofhp_dof;
		p = curproc;
		if (p->p_pid == dhp->dofhp_pid) {
			dof = dtrace_dof_copyin((uintptr_t)addr, &rval);
		} else {
			p = pfind(dhp->dofhp_pid);
			if (p == NULL)
				return (EINVAL);
			if (!P_SHOULDSTOP(p) ||
			    (p->p_flag & (P_TRACED | P_WEXIT)) != P_TRACED ||
			    p->p_pptr != curproc) {
				PROC_UNLOCK(p);
				return (EINVAL);
			}
			_PHOLD(p);
			PROC_UNLOCK(p);
			dof = dtrace_dof_copyin_proc(p, (uintptr_t)addr, &rval);
		}

		if (dof == NULL) {
			if (p != curproc)
				PRELE(p);
			break;
		}

		mutex_enter(&dtrace_lock);
		if ((rval = dtrace_helper_slurp(dof, dhp, p)) != -1) {
			dhp->dofhp_gen = rval;
			rval = 0;
		} else {
			rval = EINVAL;
		}
		mutex_exit(&dtrace_lock);
		if (p != curproc)
			PRELE(p);
		break;
	case DTRACEHIOC_REMOVE:
		mutex_enter(&dtrace_lock);
		rval = dtrace_helper_destroygen(NULL, *(int *)(uintptr_t)addr);
		mutex_exit(&dtrace_lock);
		break;
	default:
		rval = ENOTTY;
		break;
	}
	return (rval);
}

/* ARGSUSED */
static int
dtrace_ioctl(struct cdev *dev, u_long cmd, caddr_t addr,
    int flags __unused, struct thread *td)
{
	dtrace_state_t *state;
	devfs_get_cdevpriv((void **) &state);

	int error = 0;
	if (state == NULL)
		return (EINVAL);

	if (state->dts_anon) {
		ASSERT(dtrace_anon.dta_state == NULL);
		state = state->dts_anon;
	}

	switch (cmd) {
	case DTRACEIOC_AGGDESC: {
		dtrace_aggdesc_t **paggdesc = (dtrace_aggdesc_t **) addr;
		dtrace_aggdesc_t aggdesc;
		dtrace_action_t *act;
		dtrace_aggregation_t *agg;
		int nrecs;
		uint32_t offs;
		dtrace_recdesc_t *lrec;
		void *buf;
		size_t size;
		uintptr_t dest;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_AGGDESC\n",__func__,__LINE__);

		if (copyin((void *) *paggdesc, &aggdesc, sizeof (aggdesc)) != 0)
			return (EFAULT);

		mutex_enter(&dtrace_lock);

		if ((agg = dtrace_aggid2agg(state, aggdesc.dtagd_id)) == NULL) {
			mutex_exit(&dtrace_lock);
			return (EINVAL);
		}

		aggdesc.dtagd_epid = agg->dtag_ecb->dte_epid;

		nrecs = aggdesc.dtagd_nrecs;
		aggdesc.dtagd_nrecs = 0;

		offs = agg->dtag_base;
		lrec = &agg->dtag_action.dta_rec;
		aggdesc.dtagd_size = lrec->dtrd_offset + lrec->dtrd_size - offs;

		for (act = agg->dtag_first; ; act = act->dta_next) {
			ASSERT(act->dta_intuple ||
			    DTRACEACT_ISAGG(act->dta_kind));

			/*
			 * If this action has a record size of zero, it
			 * denotes an argument to the aggregating action.
			 * Because the presence of this record doesn't (or
			 * shouldn't) affect the way the data is interpreted,
			 * we don't copy it out to save user-level the
			 * confusion of dealing with a zero-length record.
			 */
			if (act->dta_rec.dtrd_size == 0) {
				ASSERT(agg->dtag_hasarg);
				continue;
			}

			aggdesc.dtagd_nrecs++;

			if (act == &agg->dtag_action)
				break;
		}

		/*
		 * Now that we have the size, we need to allocate a temporary
		 * buffer in which to store the complete description.  We need
		 * the temporary buffer to be able to drop dtrace_lock()
		 * across the copyout(), below.
		 */
		size = sizeof (dtrace_aggdesc_t) +
		    (aggdesc.dtagd_nrecs * sizeof (dtrace_recdesc_t));

		buf = kmem_alloc(size, KM_SLEEP);
		dest = (uintptr_t)buf;

		bcopy(&aggdesc, (void *)dest, sizeof (aggdesc));
		dest += offsetof(dtrace_aggdesc_t, dtagd_rec[0]);

		for (act = agg->dtag_first; ; act = act->dta_next) {
			dtrace_recdesc_t rec = act->dta_rec;

			/*
			 * See the comment in the above loop for why we pass
			 * over zero-length records.
			 */
			if (rec.dtrd_size == 0) {
				ASSERT(agg->dtag_hasarg);
				continue;
			}

			if (nrecs-- == 0)
				break;

			rec.dtrd_offset -= offs;
			bcopy(&rec, (void *)dest, sizeof (rec));
			dest += sizeof (dtrace_recdesc_t);

			if (act == &agg->dtag_action)
				break;
		}

		mutex_exit(&dtrace_lock);

		if (copyout(buf, (void *) *paggdesc, dest - (uintptr_t)buf) != 0) {
			kmem_free(buf, size);
			return (EFAULT);
		}

		kmem_free(buf, size);
		return (0);
	}
	case DTRACEIOC_AGGSNAP:
	case DTRACEIOC_BUFSNAP: {
		dtrace_bufdesc_t **pdesc = (dtrace_bufdesc_t **) addr;
		dtrace_bufdesc_t desc;
		caddr_t cached;
		dtrace_buffer_t *buf;

		dtrace_debug_output();

		if (copyin((void *) *pdesc, &desc, sizeof (desc)) != 0)
			return (EFAULT);

		DTRACE_IOCTL_PRINTF("%s(%d): %s curcpu %d cpu %d\n",
		    __func__,__LINE__,
		    cmd == DTRACEIOC_AGGSNAP ?
		    "DTRACEIOC_AGGSNAP":"DTRACEIOC_BUFSNAP",
		    curcpu, desc.dtbd_cpu);

		if (desc.dtbd_cpu >= MAXCPU || CPU_ABSENT(desc.dtbd_cpu))
			return (ENOENT);

		mutex_enter(&dtrace_lock);

		if (cmd == DTRACEIOC_BUFSNAP) {
			buf = &state->dts_buffer[desc.dtbd_cpu];
		} else {
			buf = &state->dts_aggbuffer[desc.dtbd_cpu];
		}

		if (buf->dtb_flags & (DTRACEBUF_RING | DTRACEBUF_FILL)) {
			size_t sz = buf->dtb_offset;

			if (state->dts_activity != DTRACE_ACTIVITY_STOPPED) {
				mutex_exit(&dtrace_lock);
				return (EBUSY);
			}

			/*
			 * If this buffer has already been consumed, we're
			 * going to indicate that there's nothing left here
			 * to consume.
			 */
			if (buf->dtb_flags & DTRACEBUF_CONSUMED) {
				mutex_exit(&dtrace_lock);

				desc.dtbd_size = 0;
				desc.dtbd_drops = 0;
				desc.dtbd_errors = 0;
				desc.dtbd_oldest = 0;
				sz = sizeof (desc);

				if (copyout(&desc, (void *) *pdesc, sz) != 0)
					return (EFAULT);

				return (0);
			}

			/*
			 * If this is a ring buffer that has wrapped, we want
			 * to copy the whole thing out.
			 */
			if (buf->dtb_flags & DTRACEBUF_WRAPPED) {
				dtrace_buffer_polish(buf);
				sz = buf->dtb_size;
			}

			if (copyout(buf->dtb_tomax, desc.dtbd_data, sz) != 0) {
				mutex_exit(&dtrace_lock);
				return (EFAULT);
			}

			desc.dtbd_size = sz;
			desc.dtbd_drops = buf->dtb_drops;
			desc.dtbd_errors = buf->dtb_errors;
			desc.dtbd_oldest = buf->dtb_xamot_offset;
			desc.dtbd_timestamp = dtrace_gethrtime();

			mutex_exit(&dtrace_lock);

			if (copyout(&desc, (void *) *pdesc, sizeof (desc)) != 0)
				return (EFAULT);

			buf->dtb_flags |= DTRACEBUF_CONSUMED;

			return (0);
		}

		if (buf->dtb_tomax == NULL) {
			ASSERT(buf->dtb_xamot == NULL);
			mutex_exit(&dtrace_lock);
			return (ENOENT);
		}

		cached = buf->dtb_tomax;
		ASSERT(!(buf->dtb_flags & DTRACEBUF_NOSWITCH));

		dtrace_xcall(desc.dtbd_cpu,
		    (dtrace_xcall_t)dtrace_buffer_switch, buf);

		state->dts_errors += buf->dtb_xamot_errors;

		/*
		 * If the buffers did not actually switch, then the cross call
		 * did not take place -- presumably because the given CPU is
		 * not in the ready set.  If this is the case, we'll return
		 * ENOENT.
		 */
		if (buf->dtb_tomax == cached) {
			ASSERT(buf->dtb_xamot != cached);
			mutex_exit(&dtrace_lock);
			return (ENOENT);
		}

		ASSERT(cached == buf->dtb_xamot);

		DTRACE_IOCTL_PRINTF("%s(%d): copyout the buffer snapshot\n",__func__,__LINE__);

		/*
		 * We have our snapshot; now copy it out.
		 */
		if (copyout(buf->dtb_xamot, desc.dtbd_data,
		    buf->dtb_xamot_offset) != 0) {
			mutex_exit(&dtrace_lock);
			return (EFAULT);
		}

		desc.dtbd_size = buf->dtb_xamot_offset;
		desc.dtbd_drops = buf->dtb_xamot_drops;
		desc.dtbd_errors = buf->dtb_xamot_errors;
		desc.dtbd_oldest = 0;
		desc.dtbd_timestamp = buf->dtb_switched;

		mutex_exit(&dtrace_lock);

		DTRACE_IOCTL_PRINTF("%s(%d): copyout buffer desc: size %zd drops %lu errors %lu\n",__func__,__LINE__,(size_t) desc.dtbd_size,(u_long) desc.dtbd_drops,(u_long) desc.dtbd_errors);

		/*
		 * Finally, copy out the buffer description.
		 */
		if (copyout(&desc, (void *) *pdesc, sizeof (desc)) != 0)
			return (EFAULT);

		return (0);
	}
	case DTRACEIOC_CONF: {
		dtrace_conf_t conf;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_CONF\n",__func__,__LINE__);

		bzero(&conf, sizeof (conf));
		conf.dtc_difversion = DIF_VERSION;
		conf.dtc_difintregs = DIF_DIR_NREGS;
		conf.dtc_diftupregs = DIF_DTR_NREGS;
		conf.dtc_ctfmodel = CTF_MODEL_NATIVE;

		*((dtrace_conf_t *) addr) = conf;

		return (0);
	}
	case DTRACEIOC_DOFGET: {
		dof_hdr_t **pdof = (dof_hdr_t **) addr;
		dof_hdr_t hdr, *dof = *pdof;
		int rval;
		uint64_t len;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_DOFGET\n",__func__,__LINE__);

		if (copyin((void *)dof, &hdr, sizeof (hdr)) != 0)
			return (EFAULT);

		mutex_enter(&dtrace_lock);
		dof = dtrace_dof_create(state);
		mutex_exit(&dtrace_lock);

		len = MIN(hdr.dofh_loadsz, dof->dofh_loadsz);
		rval = copyout(dof, (void *) *pdof, len);
		dtrace_dof_destroy(dof);

		return (rval == 0 ? 0 : EFAULT);
	}
	case DTRACEIOC_ENABLE: {
		dof_hdr_t *dof = NULL;
		dtrace_enabling_t *enab = NULL;
		dtrace_vstate_t *vstate;
		int err = 0;
		int rval;
		dtrace_enable_io_t *p = (dtrace_enable_io_t *) addr;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_ENABLE\n",__func__,__LINE__);

		/*
		 * If a NULL argument has been passed, we take this as our
		 * cue to reevaluate our enablings.
		 */
		if (p->dof == NULL) {
			dtrace_enabling_matchall();

			return (0);
		}

		if ((dof = dtrace_dof_copyin((uintptr_t) p->dof, &rval)) == NULL)
			return (EINVAL);

		mutex_enter(&cpu_lock);
		mutex_enter(&dtrace_lock);
		vstate = &state->dts_vstate;

		if (state->dts_activity != DTRACE_ACTIVITY_INACTIVE) {
			mutex_exit(&dtrace_lock);
			mutex_exit(&cpu_lock);
			dtrace_dof_destroy(dof);
			return (EBUSY);
		}

		if (dtrace_dof_slurp(dof, vstate, td->td_ucred, &enab, 0, 0,
		    B_TRUE) != 0) {
			mutex_exit(&dtrace_lock);
			mutex_exit(&cpu_lock);
			dtrace_dof_destroy(dof);
			return (EINVAL);
		}

		if ((rval = dtrace_dof_options(dof, state)) != 0) {
			dtrace_enabling_destroy(enab);
			mutex_exit(&dtrace_lock);
			mutex_exit(&cpu_lock);
			dtrace_dof_destroy(dof);
			return (rval);
		}

		if ((err = dtrace_enabling_match(enab, &p->n_matched)) == 0) {
			err = dtrace_enabling_retain(enab);
		} else {
			dtrace_enabling_destroy(enab);
		}

		mutex_exit(&cpu_lock);
		mutex_exit(&dtrace_lock);
		dtrace_dof_destroy(dof);

		return (err);
	}
	case DTRACEIOC_EPROBE: {
		dtrace_eprobedesc_t **pepdesc = (dtrace_eprobedesc_t **) addr;
		dtrace_eprobedesc_t epdesc;
		dtrace_ecb_t *ecb;
		dtrace_action_t *act;
		void *buf;
		size_t size;
		uintptr_t dest;
		int nrecs;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_EPROBE\n",__func__,__LINE__);

		if (copyin((void *)*pepdesc, &epdesc, sizeof (epdesc)) != 0)
			return (EFAULT);

		mutex_enter(&dtrace_lock);

		if ((ecb = dtrace_epid2ecb(state, epdesc.dtepd_epid)) == NULL) {
			mutex_exit(&dtrace_lock);
			return (EINVAL);
		}

		if (ecb->dte_probe == NULL) {
			mutex_exit(&dtrace_lock);
			return (EINVAL);
		}

		epdesc.dtepd_probeid = ecb->dte_probe->dtpr_id;
		epdesc.dtepd_uarg = ecb->dte_uarg;
		epdesc.dtepd_size = ecb->dte_size;

		nrecs = epdesc.dtepd_nrecs;
		epdesc.dtepd_nrecs = 0;
		for (act = ecb->dte_action; act != NULL; act = act->dta_next) {
			if (DTRACEACT_ISAGG(act->dta_kind) || act->dta_intuple)
				continue;

			epdesc.dtepd_nrecs++;
		}

		/*
		 * Now that we have the size, we need to allocate a temporary
		 * buffer in which to store the complete description.  We need
		 * the temporary buffer to be able to drop dtrace_lock()
		 * across the copyout(), below.
		 */
		size = sizeof (dtrace_eprobedesc_t) +
		    (epdesc.dtepd_nrecs * sizeof (dtrace_recdesc_t));

		buf = kmem_alloc(size, KM_SLEEP);
		dest = (uintptr_t)buf;

		bcopy(&epdesc, (void *)dest, sizeof (epdesc));
		dest += offsetof(dtrace_eprobedesc_t, dtepd_rec[0]);

		for (act = ecb->dte_action; act != NULL; act = act->dta_next) {
			if (DTRACEACT_ISAGG(act->dta_kind) || act->dta_intuple)
				continue;

			if (nrecs-- == 0)
				break;

			bcopy(&act->dta_rec, (void *)dest,
			    sizeof (dtrace_recdesc_t));
			dest += sizeof (dtrace_recdesc_t);
		}

		mutex_exit(&dtrace_lock);

		if (copyout(buf, (void *) *pepdesc, dest - (uintptr_t)buf) != 0) {
			kmem_free(buf, size);
			return (EFAULT);
		}

		kmem_free(buf, size);
		return (0);
	}
	case DTRACEIOC_FORMAT: {
		dtrace_fmtdesc_t *fmt = (dtrace_fmtdesc_t *) addr;
		char *str;
		int len;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_FORMAT\n",__func__,__LINE__);

		mutex_enter(&dtrace_lock);

		if (fmt->dtfd_format == 0 ||
		    fmt->dtfd_format > state->dts_nformats) {
			mutex_exit(&dtrace_lock);
			return (EINVAL);
		}

		/*
		 * Format strings are allocated contiguously and they are
		 * never freed; if a format index is less than the number
		 * of formats, we can assert that the format map is non-NULL
		 * and that the format for the specified index is non-NULL.
		 */
		ASSERT(state->dts_formats != NULL);
		str = state->dts_formats[fmt->dtfd_format - 1];
		ASSERT(str != NULL);

		len = strlen(str) + 1;

		if (len > fmt->dtfd_length) {
			fmt->dtfd_length = len;
		} else {
			if (copyout(str, fmt->dtfd_string, len) != 0) {
				mutex_exit(&dtrace_lock);
				return (EINVAL);
			}
		}

		mutex_exit(&dtrace_lock);
		return (0);
	}
	case DTRACEIOC_GO: {
		int rval;
		processorid_t *cpuid = (processorid_t *) addr;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_GO\n",__func__,__LINE__);

		rval = dtrace_state_go(state, cpuid);

		return (rval);
	}
	case DTRACEIOC_PROBEARG: {
		dtrace_argdesc_t *desc = (dtrace_argdesc_t *) addr;
		dtrace_probe_t *probe;
		dtrace_provider_t *prov;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_PROBEARG\n",__func__,__LINE__);

		if (desc->dtargd_id == DTRACE_IDNONE)
			return (EINVAL);

		if (desc->dtargd_ndx == DTRACE_ARGNONE)
			return (EINVAL);

		mutex_enter(&dtrace_provider_lock);
#ifdef illumos
		mutex_enter(&mod_lock);
#endif
		mutex_enter(&dtrace_lock);

		if (desc->dtargd_id > dtrace_nprobes) {
			mutex_exit(&dtrace_lock);
#ifdef illumos
			mutex_exit(&mod_lock);
#endif
			mutex_exit(&dtrace_provider_lock);
			return (EINVAL);
		}

		if ((probe = dtrace_probes[desc->dtargd_id - 1]) == NULL) {
			mutex_exit(&dtrace_lock);
#ifdef illumos
			mutex_exit(&mod_lock);
#endif
			mutex_exit(&dtrace_provider_lock);
			return (EINVAL);
		}

		mutex_exit(&dtrace_lock);

		prov = probe->dtpr_provider;

		if (prov->dtpv_pops.dtps_getargdesc == NULL) {
			/*
			 * There isn't any typed information for this probe.
			 * Set the argument number to DTRACE_ARGNONE.
			 */
			desc->dtargd_ndx = DTRACE_ARGNONE;
		} else {
			desc->dtargd_native[0] = '\0';
			desc->dtargd_xlate[0] = '\0';
			desc->dtargd_mapping = desc->dtargd_ndx;

			prov->dtpv_pops.dtps_getargdesc(prov->dtpv_arg,
			    probe->dtpr_id, probe->dtpr_arg, desc);
		}

#ifdef illumos
		mutex_exit(&mod_lock);
#endif
		mutex_exit(&dtrace_provider_lock);

		return (0);
	}
	case DTRACEIOC_PROBEMATCH:
	case DTRACEIOC_PROBES: {
		dtrace_probedesc_t *p_desc = (dtrace_probedesc_t *) addr;
		dtrace_probe_t *probe = NULL;
		dtrace_probekey_t pkey;
		dtrace_id_t i;
		int m = 0;
		uint32_t priv = 0;
		uid_t uid = 0;
		zoneid_t zoneid = 0;

		DTRACE_IOCTL_PRINTF("%s(%d): %s\n",__func__,__LINE__,
		    cmd == DTRACEIOC_PROBEMATCH ?
		    "DTRACEIOC_PROBEMATCH":"DTRACEIOC_PROBES");

		p_desc->dtpd_provider[DTRACE_PROVNAMELEN - 1] = '\0';
		p_desc->dtpd_mod[DTRACE_MODNAMELEN - 1] = '\0';
		p_desc->dtpd_func[DTRACE_FUNCNAMELEN - 1] = '\0';
		p_desc->dtpd_name[DTRACE_NAMELEN - 1] = '\0';

		/*
		 * Before we attempt to match this probe, we want to give
		 * all providers the opportunity to provide it.
		 */
		if (p_desc->dtpd_id == DTRACE_IDNONE) {
			mutex_enter(&dtrace_provider_lock);
			dtrace_probe_provide(p_desc, NULL);
			mutex_exit(&dtrace_provider_lock);
			p_desc->dtpd_id++;
		}

		if (cmd == DTRACEIOC_PROBEMATCH)  {
			dtrace_probekey(p_desc, &pkey);
			pkey.dtpk_id = DTRACE_IDNONE;
		}

		dtrace_cred2priv(td->td_ucred, &priv, &uid, &zoneid);

		mutex_enter(&dtrace_lock);

		if (cmd == DTRACEIOC_PROBEMATCH) {
			for (i = p_desc->dtpd_id; i <= dtrace_nprobes; i++) {
				if ((probe = dtrace_probes[i - 1]) != NULL &&
				    (m = dtrace_match_probe(probe, &pkey,
				    priv, uid, zoneid)) != 0)
					break;
			}

			if (m < 0) {
				mutex_exit(&dtrace_lock);
				return (EINVAL);
			}

		} else {
			for (i = p_desc->dtpd_id; i <= dtrace_nprobes; i++) {
				if ((probe = dtrace_probes[i - 1]) != NULL &&
				    dtrace_match_priv(probe, priv, uid, zoneid))
					break;
			}
		}

		if (probe == NULL) {
			mutex_exit(&dtrace_lock);
			return (ESRCH);
		}

		dtrace_probe_description(probe, p_desc);
		mutex_exit(&dtrace_lock);

		return (0);
	}
	case DTRACEIOC_PROVIDER: {
		dtrace_providerdesc_t *pvd = (dtrace_providerdesc_t *) addr;
		dtrace_provider_t *pvp;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_PROVIDER\n",__func__,__LINE__);

		pvd->dtvd_name[DTRACE_PROVNAMELEN - 1] = '\0';
		mutex_enter(&dtrace_provider_lock);

		for (pvp = dtrace_provider; pvp != NULL; pvp = pvp->dtpv_next) {
			if (strcmp(pvp->dtpv_name, pvd->dtvd_name) == 0)
				break;
		}

		mutex_exit(&dtrace_provider_lock);

		if (pvp == NULL)
			return (ESRCH);

		bcopy(&pvp->dtpv_priv, &pvd->dtvd_priv, sizeof (dtrace_ppriv_t));
		bcopy(&pvp->dtpv_attr, &pvd->dtvd_attr, sizeof (dtrace_pattr_t));

		return (0);
	}
	case DTRACEIOC_REPLICATE: {
		dtrace_repldesc_t *desc = (dtrace_repldesc_t *) addr;
		dtrace_probedesc_t *match = &desc->dtrpd_match;
		dtrace_probedesc_t *create = &desc->dtrpd_create;
		int err;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_REPLICATE\n",__func__,__LINE__);

		match->dtpd_provider[DTRACE_PROVNAMELEN - 1] = '\0';
		match->dtpd_mod[DTRACE_MODNAMELEN - 1] = '\0';
		match->dtpd_func[DTRACE_FUNCNAMELEN - 1] = '\0';
		match->dtpd_name[DTRACE_NAMELEN - 1] = '\0';

		create->dtpd_provider[DTRACE_PROVNAMELEN - 1] = '\0';
		create->dtpd_mod[DTRACE_MODNAMELEN - 1] = '\0';
		create->dtpd_func[DTRACE_FUNCNAMELEN - 1] = '\0';
		create->dtpd_name[DTRACE_NAMELEN - 1] = '\0';

		mutex_enter(&dtrace_lock);
		err = dtrace_enabling_replicate(state, match, create);
		mutex_exit(&dtrace_lock);

		return (err);
	}
	case DTRACEIOC_STATUS: {
		dtrace_status_t *stat = (dtrace_status_t *) addr;
		dtrace_dstate_t *dstate;
		int i, j;
		uint64_t nerrs;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_STATUS\n",__func__,__LINE__);

		/*
		 * See the comment in dtrace_state_deadman() for the reason
		 * for setting dts_laststatus to INT64_MAX before setting
		 * it to the correct value.
		 */
		state->dts_laststatus = INT64_MAX;
		dtrace_membar_producer();
		state->dts_laststatus = dtrace_gethrtime();

		bzero(stat, sizeof (*stat));

		mutex_enter(&dtrace_lock);

		if (state->dts_activity == DTRACE_ACTIVITY_INACTIVE) {
			mutex_exit(&dtrace_lock);
			return (ENOENT);
		}

		if (state->dts_activity == DTRACE_ACTIVITY_DRAINING)
			stat->dtst_exiting = 1;

		nerrs = state->dts_errors;
		dstate = &state->dts_vstate.dtvs_dynvars;

		CPU_FOREACH(i) {
			dtrace_dstate_percpu_t *dcpu = &dstate->dtds_percpu[i];

			stat->dtst_dyndrops += dcpu->dtdsc_drops;
			stat->dtst_dyndrops_dirty += dcpu->dtdsc_dirty_drops;
			stat->dtst_dyndrops_rinsing += dcpu->dtdsc_rinsing_drops;

			if (state->dts_buffer[i].dtb_flags & DTRACEBUF_FULL)
				stat->dtst_filled++;

			nerrs += state->dts_buffer[i].dtb_errors;

			for (j = 0; j < state->dts_nspeculations; j++) {
				dtrace_speculation_t *spec;
				dtrace_buffer_t *buf;

				spec = &state->dts_speculations[j];
				buf = &spec->dtsp_buffer[i];
				stat->dtst_specdrops += buf->dtb_xamot_drops;
			}
		}

		stat->dtst_specdrops_busy = state->dts_speculations_busy;
		stat->dtst_specdrops_unavail = state->dts_speculations_unavail;
		stat->dtst_stkstroverflows = state->dts_stkstroverflows;
		stat->dtst_dblerrors = state->dts_dblerrors;
		stat->dtst_killed =
		    (state->dts_activity == DTRACE_ACTIVITY_KILLED);
		stat->dtst_errors = nerrs;

		mutex_exit(&dtrace_lock);

		return (0);
	}
	case DTRACEIOC_STOP: {
		int rval;
		processorid_t *cpuid = (processorid_t *) addr;

		DTRACE_IOCTL_PRINTF("%s(%d): DTRACEIOC_STOP\n",__func__,__LINE__);

		mutex_enter(&dtrace_lock);
		rval = dtrace_state_stop(state, cpuid);
		mutex_exit(&dtrace_lock);

		return (rval);
	}
	default:
		error = ENOTTY;
	}
	return (error);
}
