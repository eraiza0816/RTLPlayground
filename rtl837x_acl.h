#ifndef _RTL837X_ACL_H_
#define _RTL837X_ACL_H_

#include <stdint.h>

/* Rule templates used by the initial ACL implementation */
#define ACL_TPL_MAC		0
#define ACL_TPL_IP		1
#define ACL_TPL_VLAN		4

/* Match field types (also the template selector) */
#define ACL_MATCH_MAC		0
#define ACL_MATCH_IP		1
#define ACL_MATCH_VLAN		2

/* Scratch for the rule builder, filled by the caller (cmd_parser) */
extern __xdata uint16_t acl_field[8];
extern __xdata uint16_t acl_care[8];

void acl_setup(void) __banked;
void acl_enable(__xdata uint8_t enable) __banked;
uint8_t acl_rule_add(__xdata uint8_t port, __xdata uint8_t action, __xdata uint8_t template) __banked;
void acl_rule_del(__xdata uint8_t idx) __banked;
void acl_show(void) __banked;

#endif
