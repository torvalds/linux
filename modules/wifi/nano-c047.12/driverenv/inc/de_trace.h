
/* $Id$ */

/*!
  * @file de_trace.h
  *
  * Adaptation layer for trace functionality
  *
  */

#ifndef DE_TRACE_H
#define DE_TRACE_H

/** @defgroup driverenv_trace_api Driver Environment Trace API functions
 *  @{
 */


/*! Bits for enabling/disabling tracing categories.
 * Low 16 bits are used by WiFiEngine, remaining bits can be used by
 * the driver for its own classes.
 */
extern uint32_t trace_mask;
#define TR_ALWAYS      (~0)
#define TR_ALL         (~0)
#define TR_ASSOC       (1 << 0)
#define TR_DATA        (1 << 1)
#define TR_AUTH        (1 << 2)
#define TR_SM          (1 << 3)
#define TR_MIB         (1 << 4)
#define TR_CONSOLE     (1 << 5)
#define TR_INITIALIZE  (1 << 6)
#define TR_CMD         (1 << 7)
// WifiEngine Internal
#define TR_WEI         (1 << 8)
#define TR_WRAP        (1 << 9)
#define TR_PS          (1 << 10)
#define TR_NETLIST     (1 << 11)
#define TR_CB          (1 << 12)
#define TR_IBSS        (1 << 13)
#define TR_WMM_PS      (1 << 14)
#define TR_XX          (1 << 15)
#define TR_ROAM        (1 << 16)
#define TR_WPA         (1 << 17)
#define TR_XY          (1 << 18)
#define TR_TRANSPORT   (1 << 19)
#define TR_API         (1 << 20) //Used for logging comunication between our driver and OS
#define TR_DATA_DUMP   (1 << 21)
#define TR_SCAN        (1 << 22)
#define TR_STATISTICS  (1 << 23)
#define TR_PROF        (1 << 24)
#define TR_ESP         (1 << 25)
#define TR_PCAP        (1 << 26)
#define TR_NOISE       (1 << 27)
#define TR_HIGH_RES    (1 << 27)
#define TR_CM          (1 << 26)
#define TR_AMP         (1 << 28)
#define TR_PMKID        TR_ASSOC
#define TR_WPS          TR_WPA

#define TR_SPI                   TR_TRANSPORT
#define TR_SPI_DEBUG             0

// some hi resolution traces generating lots of logs
// set to 0 will hopefully be optimized out by the compiler
#define TR_NET_FILTER            TR_HIGH_RES
#define TR_DE_DEBUG              TR_HIGH_RES
#define TR_PS_DEBUG              TR_HIGH_RES
#define TR_CMD_DEBUG             TR_HIGH_RES
#define TR_SM_HIGH_RES           TR_HIGH_RES
#define TR_DATA_HIGH_RES         TR_HIGH_RES
#define TR_TRANSPORT_HIGH_RES    TR_HIGH_RES
#define TR_CB_HIGH_RES           TR_HIGH_RES
#define TR_MIB_HIGH_RES          TR_HIGH_RES
#define TR_ROAM_HIGH_RES         TR_HIGH_RES
#define TR_AMP_HIGH_RES          TR_HIGH_RES

/* don't use TR_ALL for potentially harmful event. 
 * And don't cry wolf! 
 * use DE_BUG_ON on fatal */
#define TR_WARN         TR_ALWAYS // Potentially harmfyl
#define TR_SEVERE       TR_ALWAYS // Severe but hopefully recoverable

#define DE_INITIAL_TRACE_MASK TR_ALL

#ifdef WIFI_DEBUG_ON

extern uint32_t trace_mask;
#define TRACE_ENABLED(TR) (trace_mask & (TR))
#define DE_TRACEN(_tr, _fmt, ...) ({if(trace_mask & (_tr)) printk(_fmt, ##__VA_ARGS__); })
#define DE_TRACE(_fmt, ...) printk(_fmt , ##__VA_ARGS__)
#define DE_TRACE_ALWAYS DE_TRACE
extern struct log_t logger;

#else /* WIFI_DEBUG_ON */

 //#define DE_BUG_ON(cond,msg)
#define TRACE_ENABLED(TR) (0)
#define DE_TRACE(fmt, args...) ({0;})
#define DE_TRACEN(_tr, _fmt, ...) ({0;})
#define DE_TRACE_ALWAYS(fmt, args...) ({0;})

#endif /* WIFI_DEBUG_ON */

#ifdef C_LOGGING

extern struct log_t logger;
#define DE_TRACE_STATIC(_tr, m)            ({if(trace_mask & (_tr)) LOG_STATIC(&logger, #_tr ": " m); })
#define DE_TRACE_STATIC2(_tr, m,a)         ({if(trace_mask & (_tr)) LOG_STATIC(&logger, #_tr ": " m, a); })
#define DE_TRACE_STATIC3(_tr, m,a,b)       ({if(trace_mask & (_tr)) LOG_STATIC(&logger, #_tr ": " m, a, b); })
#define DE_TRACE_PTR(_tr, m, p)            ({if(trace_mask & (_tr)) LOG_PTR(&logger,    #_tr ": " m,p); })
#define DE_TRACE_PTR2(_tr, m, a,b)         ({if(trace_mask & (_tr)) LOG_PTR(&logger,    #_tr ": " m,a,b); })
#define DE_TRACE_PTR3(_tr, m, a,b,c)       ({if(trace_mask & (_tr)) LOG_PTR(&logger,    #_tr ": " m,a,b,c); })
#define DE_TRACE_INT(_tr, m, a)            ({if(trace_mask & (_tr)) LOG_INT(&logger,    #_tr ": " m,1,a,0,0,0,0,0); })
#define DE_TRACE_INT2(_tr, m, a,b)         ({if(trace_mask & (_tr)) LOG_INT(&logger,    #_tr ": " m,2,a,b,0,0,0,0); }) 
#define DE_TRACE_INT3(_tr, m, a,b,c)       ({if(trace_mask & (_tr)) LOG_INT(&logger,    #_tr ": " m,3,a,b,c,0,0,0); })
#define DE_TRACE_INT4(_tr, m, a,b,c,d)     ({if(trace_mask & (_tr)) LOG_INT(&logger,    #_tr ": " m,4,a,b,c,d,0,0); })
#define DE_TRACE_INT5(_tr, m, a,b,c,d,e)   ({if(trace_mask & (_tr)) LOG_INT(&logger,    #_tr ": " m,4,a,b,c,d,e,0); })
#define DE_TRACE_INT6(_tr, m, a,b,c,d,e,f) ({if(trace_mask & (_tr)) LOG_INT(&logger,    #_tr ": " m,4,a,b,c,d,e,f); })
#define DE_TRACE_DATA(_tr, m, data, len)   ({if(trace_mask & (_tr)) LOG_DATA(&logger,   #_tr ": " m,data,len); })
#define DE_TRACE_MIB(_tr, m, data, len)    ({if(trace_mask & (_tr)) LOG_MIB(&logger,    #_tr ": " m,data,len); })
#define DE_TRACE_STRING(_tr, m, string)    ({if(trace_mask & (_tr)) LOG_STRING(&logger, #_tr ": " m,string); })
#define DE_TRACE_STRING2(_tr, m, a,b)      ({if(trace_mask & (_tr)) LOG_STRING(&logger, #_tr ": " m,a,b); })
#define DE_TRACE_STRING3(_tr, m, a,b,c)    ({if(trace_mask & (_tr)) LOG_STRING(&logger, #_tr ": " m,a,b,c); })

/* DE_TRACE_VA exists for backward compatibility */
/* must only be used in driverenv.h */
#define DE_TRACE_VA(_tr, _fmt, ...)        ({if(trace_mask & (_tr)) LOG_VA(&logger, _fmt , ##__VA_ARGS__ ); })
#define MAX_LOG_STR_LEN 300
#define LOG_VA(logger,_fmt, ... ) \
do { \
    struct log_event_t __event__; \
    unsigned char __payload__[MAX_LOG_STR_LEN]; \
    __event__.file = __FILE__; \
    __event__.func = __func__; \
    __event__.line = __LINE__; \
    __event__.message = "%s"; \
    __event__.data_type = LOGDATA_STRING; \
    __event__.data_len = scnprintf(__payload__, MAX_LOG_STR_LEN, _fmt ,  ## __VA_ARGS__ ); \
    if(__event__.data_len>0) __event__.data_len++; \
    logger_put(logger,&__event__,__payload__); \
} while(0)

#define DE_TRACE1 DE_TRACE_VA
#define DE_TRACE2 DE_TRACE_VA
#define DE_TRACE3 DE_TRACE_VA
#define DE_TRACE4 DE_TRACE_VA
#define DE_TRACE5 DE_TRACE_VA
#define DE_TRACE6 DE_TRACE_VA
#define DE_TRACE7 DE_TRACE_VA
#define DE_TRACE8 DE_TRACE_VA
#define DE_TRACE9 DE_TRACE_VA
#define DE_TRACE_STATIC3 DE_TRACE_VA
#define DE_TRACE_STATIC4 DE_TRACE_VA

#else /* !C_LOGGING */

#define DE_TRACE_STATIC(_tr, m)              DE_TRACEN(_tr, #_tr ": [%s] " m, __func__)
#define DE_TRACE_STATIC2(_tr, m, a)          DE_TRACEN(_tr, #_tr ": [%s] " m, __func__,a)
#define DE_TRACE_STATIC3(_tr, m, a,b)        DE_TRACEN(_tr, #_tr ": [%s] " m, __func__,a,b)
#define DE_TRACE_STATIC4(_tr, m, a,b,c)      DE_TRACEN(_tr, #_tr ": [%s] " m, __func__,a,b,c)
#define DE_TRACE_STATIC5(_tr, m, a,b,c,d)    DE_TRACEN(_tr, #_tr ": [%s] " m, __func__,a,b,c,d)
#define DE_TRACE_STRING(_tr, m, string)      DE_TRACEN(_tr, #_tr ": [%s] " m, __func__, string)
#define DE_TRACE_STRING2(_tr, m, sa, sb)     DE_TRACEN(_tr, #_tr ": [%s] " m, __func__, sa, sb)
#define DE_TRACE_STRING3(_tr, m, sa, sb, sc) DE_TRACEN(_tr, #_tr ": [%s] " m, __func__, sa, sb, sc)
#define DE_TRACE_PTR(_tr, m, p)              DE_TRACEN(_tr, #_tr ": [%s] " m, __func__, p)
#define DE_TRACE_INT(_tr, m, a)              DE_TRACEN(_tr, #_tr ": [%s] " m, __func__, a)
#define DE_TRACE_INT2(_tr, m, a,b)           DE_TRACEN(_tr, #_tr ": [%s] " m, __func__, a, b)
#define DE_TRACE_INT3(_tr, m, a,b,c)         DE_TRACEN(_tr, #_tr ": [%s] " m, __func__, a, b, c)
#define DE_TRACE_INT4(_tr, m, a,b,c,d)       DE_TRACEN(_tr, #_tr ": [%s] " m, __func__, a, b, c, d)
#define DE_TRACE_INT5(_tr, m, a,b,c,d,e)     DE_TRACEN(_tr, #_tr ": [%s] " m, __func__, a, b, c, d, e)
#define DE_TRACE_INT6(_tr, m, a,b,c,d,e,f)   DE_TRACEN(_tr, #_tr ": [%s] " m, __func__, a, b, c, d, e, f)
#define DE_TRACE_DATA(_tr, m,data, len)      DE_TRACEN(_tr, #_tr ": [%s] " m, __func__)
#define DE_TRACE_MIB(_tr, m, data, len)
#define DE_TRACE_VA(_tr, _fmt, ...)          DE_TRACEN(_tr, _fmt , ##__VA_ARGS__ )


#define DE_TRACE1 DE_TRACEN
#define DE_TRACE2 DE_TRACEN
#define DE_TRACE3 DE_TRACEN
#define DE_TRACE4 DE_TRACEN
#define DE_TRACE5 DE_TRACEN
#define DE_TRACE6 DE_TRACEN
#define DE_TRACE7 DE_TRACEN
#define DE_TRACE8 DE_TRACEN
#define DE_TRACE9 DE_TRACEN

#endif /* C_LOGGING */


#define DBG_PRINTBUF(prefix, buf, len) KDEBUG_BUF(PRINTBUF, (buf), (len), (prefix))

#if 0 /* Stack usage tracing is obsolete */
void de_trace_stack_usage(void);
#define DE_TRACE_STACK_USAGE de_trace_stack_usage()
#else
#define DE_TRACE_STACK_USAGE
#endif
#define  DE_STRTOUL(a,b,c) simple_strtoul((a), (b), (c))
/*!
 * \brief Assert macro. 
 * 
 * Assert macro to add diagnostics to the program .
 * If expression evaluates to 0 (false), then the expression, sourcecode filename,
 * and line number are sent to the standard error, and then calls the abort function.
 * If the identifier NDEBUG ("no debug") is defined with #define NDEBUG then the macro
 * assert does nothing.
 *
 * @param _exp Pointer to buffer.
 * 
 * @return void.
 */
#define  DE_ASSERT(_exp) do { if(!(_exp)) { printk(KERN_ERR "%s:%s:%d: assert failed", __FILE__, __func__, __LINE__); BUG(); } } while(0)

// more extensive asserts; match to trace levels/modules
#define  DE_SM_ASSERT(_exp) do { if(!(_exp)) { printk(KERN_ERR "%s:%s:%d: assert failed", __FILE__, __func__, __LINE__); } } while(0)


#undef DE_BUG_ON

#define DE_BUG_ON(cond, msg, args...)  do { if (unlikely((cond)!=0)) {printk(KERN_ERR msg , ##args); BUG();} } while(0)


/** @} */ /* End of driverenv_trace_api group */

#endif /* DE_TRACE_H */
