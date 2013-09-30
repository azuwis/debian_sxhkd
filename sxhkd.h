/* * Copyright (c) 2013 Bastien Dejean
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SXHKD_H
#define _SXHKD_H

#include <xcb/xcb_keysyms.h>
#include <stdio.h>
#include <stdbool.h>
#include "types.h"
#include "helpers.h"

#define CONFIG_HOME_ENV  "XDG_CONFIG_HOME"
#define SXHKD_SHELL_ENV  "SXHKD_SHELL"
#define SHELL_ENV        "SHELL"
#define CONFIG_PATH      "sxhkd/sxhkdrc"
#define HOTKEY_PREFIX    'H'
#define COMMAND_PREFIX   'C'
#define TIMEOUT_PREFIX   'T'
#define TIMEOUT          3

xcb_connection_t *dpy;
xcb_window_t root;
xcb_key_symbols_t *symbols;

char *shell;
char config_file[MAXLEN];
char *config_path;
char **extra_confs;
int num_extra_confs;
int redir_fd;
FILE *status_fifo;
char progress[MAXLEN];
bool ignore_mapping;
int timeout;

hotkey_t *hotkeys_head, *hotkeys_tail;
bool running, reload, bell, chained, locked;
chord_t *escape_chord;

uint16_t num_lock;
uint16_t caps_lock;
uint16_t scroll_lock;

void key_button_event(xcb_generic_event_t *, uint8_t);
void motion_notify(xcb_generic_event_t *, uint8_t);
void mapping_notify(xcb_generic_event_t *);
void setup(void);
void cleanup(void);
void reload_cmd(void);
void hold(int);
void put_status(char, char *);

#endif
