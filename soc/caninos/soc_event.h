#ifndef __PUPPY_SOC_EVENT_H__
#define __PUPPY_SOC_EVENT_H__

#define PULP_SOC_EU_ADDR 0x1A106000

#define PULP_EU_SW_EVENT_REG (PULP_SOC_EU_ADDR + 0x00)
#define PULP_EU_FC_MASK0_REG (PULP_SOC_EU_ADDR + 0x04)
#define PULP_EU_FC_MASK1_REG (PULP_SOC_EU_ADDR + 0x08)
#define PULP_EU_FC_MASK2_REG (PULP_SOC_EU_ADDR + 0x0C)
#define PULP_EU_FC_MASK3_REG (PULP_SOC_EU_ADDR + 0x10)
#define PULP_EU_FC_MASK4_REG (PULP_SOC_EU_ADDR + 0x14)
#define PULP_EU_FC_MASK5_REG (PULP_SOC_EU_ADDR + 0x18)
#define PULP_EU_FC_MASK6_REG (PULP_SOC_EU_ADDR + 0x1C)
#define PULP_EU_FC_MASK7_REG (PULP_SOC_EU_ADDR + 0x20)

#define SOC_NB_EVENT_REGS 8
#define SOC_MAX_NUM_EVENT_CALLBACKS 32

#ifndef _ASMLANGUAGE

typedef void (*event_callback_t)(unsigned int event_num, void *userdata);

extern int puppy_event_register_callback(event_callback_t callback, void *userdata);

extern int puppy_event_unregister_callback(event_callback_t callback, void* userdata);

extern int puppy_event_is_enabled(unsigned int event_num);

extern int puppy_event_disable(unsigned int event_num);

extern int puppy_event_enable(unsigned int event_num);

#endif // _ASMLANGUAGE
#endif // __PUPPY_SOC_EVENT_H__
