/*
** Copyright 2021 Ralph Irving. All Rights Reserved.
**
** This file is licensed under BSD. Please see the LICENSE file for details.
*/
#define RUNTIME_DEBUG 0

#include "jive.h"

extern LOG_CATEGORY *log_ui;
extern Uint32 bsp_get_realtime_millis (void);

/*
 * IR input will cause the following events to be sent:
 * EVENT_IR_DOWN - sent as soon as a new code has been sent
 * EVENT_IR_REPEAT - sent out as fast as ir codes are received after IR_DOWN occurs
 * EVENT_IR_PRESS - sent when no new codes are received (or a new key is pressed) prior to the IR_HOLD_TIMEOUT time
 * EVENT_IR_HOLD - sent once when ir input occurs and the IR_HOLD_TIMEOUT time has been exceeded
 * EVENT_IR_UP - sent when the ir code changes
 */



/* button hold threshold .9 seconds - HOLD event is sent when a new ir code is received after IR_HOLD_TIMEOUT ms*/
#define IR_HOLD_TIMEOUT 900

/* time that new ir input has occurred (using the input_event time as the time source) */
Uint32 ir_down_millis = 0;

/* time that the last ir input was received (using the input_event time as the time source)*/
Uint32 ir_last_input_millis = 0;

/* last ir code received */
Uint32 ir_last_code = 0;

bool ir_received_this_loop = false;

static enum jive_ir_state {
	IR_STATE_NONE,
	IR_STATE_DOWN,
	IR_STATE_HOLD_SENT,
} ir_state = IR_STATE_NONE;


const char* getJiveEventName(JiveEventType type) 
{
	switch (type) 
	{
		case JIVE_EVENT_IR_PRESS: return "JIVE_EVENT_IR_PRESS";
		case JIVE_EVENT_IR_UP: return "JIVE_EVENT_IR_UP";
		case JIVE_EVENT_IR_DOWN: return "JIVE_EVENT_IR_DOWN";
		case JIVE_EVENT_IR_REPEAT: return "JIVE_EVENT_IR_REPEAT";
		case JIVE_EVENT_IR_HOLD: return "JIVE_EVENT_IR_HOLD";
		default: return "JIVE_EVENT_OTHER";
	}
}


static Uint32 queue_ir_event(Uint32 millis, Uint32 code, JiveEventType type) {
	JiveEvent event;

	memset(&event, 0, sizeof(JiveEvent));

	event.type = type;
	event.u.ir.code = code;
	event.ticks = millis;

	LOG_WARN(log_ui, "type: %s code: %x ms: %u", getJiveEventName(type), code, millis);

	jive_queue_event(&event);
	return 0;
}


static int ir_handle_up() {
	Uint32 now;

	now = bsp_get_realtime_millis();
	if (ir_state != IR_STATE_HOLD_SENT) {
		// code using PRESS and UP shouldn't care yet about the time....
		queue_ir_event(now, ir_last_code, (JiveEventType) JIVE_EVENT_IR_PRESS);
	}

	ir_state = IR_STATE_NONE;
	queue_ir_event(now, ir_last_code, (JiveEventType) JIVE_EVENT_IR_UP);
	
	ir_down_millis = 0;
	ir_last_input_millis = 0;
	ir_last_code = 0;
	
	return 0;
}

static int ir_handle_down(Uint32 code, Uint32 time) {
	ir_state = IR_STATE_DOWN;
	ir_down_millis = time;

	queue_ir_event(time, code, (JiveEventType) JIVE_EVENT_IR_DOWN);
					
	return 0;
}


void ir_input_code(Uint32 ir_code, Uint32 input_time) {
	ir_received_this_loop = true;

	/* IR_KEY_UP */
	if (ir_code == 0xFFFFFFFF) {
		if ( ir_last_code ) {
			ir_handle_up();
		} else {
			ir_state = IR_STATE_NONE;
		}
		return;
	}	

	switch (ir_state) {
	case IR_STATE_NONE:
		ir_handle_down(ir_code, input_time);
		break;

	case IR_STATE_DOWN:
	case IR_STATE_HOLD_SENT:
		queue_ir_event(input_time, ir_code, (JiveEventType) JIVE_EVENT_IR_REPEAT);

		if (ir_state == IR_STATE_DOWN && input_time >= ir_down_millis + IR_HOLD_TIMEOUT) {
			ir_state = IR_STATE_HOLD_SENT;
			queue_ir_event(input_time, ir_code, (JiveEventType) JIVE_EVENT_IR_HOLD);
		}
		break;
	}

	ir_last_input_millis = input_time;
	ir_last_code = ir_code;
}


void ir_input_complete(Uint32 now) {
	ir_received_this_loop = false;
}
