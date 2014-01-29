/* Copyright (c) 2013-2014, Bastien Dejean
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are those
 * of the authors and should not be interpreted as representing official policies,
 * either expressed or implied, of the FreeBSD Project.
 */

#include <xcb/xcb_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include "parse.h"
#include "grab.h"
#include "sxhkd.h"

int main(int argc, char *argv[])
{
	char opt;
	char *fifo_path = NULL;
	status_fifo = NULL;
	config_path = NULL;
	ignore_mapping = false;
	timeout = TIMEOUT;
	unsigned int max_freq = 0;
	motion_interval = 0;
	last_motion_time = 0;

	while ((opt = getopt(argc, argv, "vhnt:c:r:s:f:")) != (char)-1) {
		switch (opt) {
			case 'v':
				printf("%s\n", VERSION);
				exit(EXIT_SUCCESS);
				break;
			case 'h':
				printf("sxhkd [-h|-v|-t TIMEOUT|-c CONFIG_FILE|-r REDIR_FILE|-s STATUS_FIFO] [EXTRA_CONFIG ...]\n");
				exit(EXIT_SUCCESS);
				break;
			case 'n':
				ignore_mapping = true;
				break;
			case 't':
				timeout = atoi(optarg);
				break;
			case 'c':
				config_path = optarg;
				break;
			case 'r':
				redir_fd = open(optarg, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
				if (redir_fd == -1)
					warn("Failed to open the command redirection file.\n");
				break;
			case 's':
				fifo_path = optarg;
				break;
			case 'f':
				if (sscanf(optarg, "%u", &max_freq) != 1)
					warn("Can't parse maximum pointer frequency.\n");
				break;
		}
	}

	num_extra_confs = argc - optind;
	extra_confs = argv + optind;

	if (config_path == NULL) {
		char *config_home = getenv(CONFIG_HOME_ENV);
		if (config_home != NULL)
			snprintf(config_file, sizeof(config_file), "%s/%s", config_home, CONFIG_PATH);
		else
			snprintf(config_file, sizeof(config_file), "%s/%s/%s", getenv("HOME"), ".config", CONFIG_PATH);
	} else {
		snprintf(config_file, sizeof(config_file), "%s", config_path);
	}

	if (fifo_path != NULL) {
		int fifo_fd = open(fifo_path, O_RDWR | O_NONBLOCK);
		if (fifo_fd != -1)
			status_fifo = fdopen(fifo_fd, "w");
		else
			warn("Couldn't open status fifo.\n");
	}

	if (max_freq != 0)
		motion_interval = 1000.0 / max_freq;

	signal(SIGINT, hold);
	signal(SIGHUP, hold);
	signal(SIGTERM, hold);
	signal(SIGUSR1, hold);
	signal(SIGALRM, hold);

	setup();
	get_standard_keysyms();
	get_lock_fields();
	escape_chord = make_chord(ESCAPE_KEYSYM, XCB_NONE, 0, XCB_KEY_PRESS, false, false);
	load_config(config_file);
	for (int i = 0; i < num_extra_confs; i++)
		load_config(extra_confs[i]);
	grab();

	xcb_generic_event_t *evt;
	int fd = xcb_get_file_descriptor(dpy);

	fd_set descriptors;

	reload = bell = chained = locked = false;
	running = true;

	xcb_flush(dpy);

	while (running) {
		FD_ZERO(&descriptors);
		FD_SET(fd, &descriptors);

		if (select(fd + 1, &descriptors, NULL, NULL, NULL) > 0) {
			while ((evt = xcb_poll_for_event(dpy)) != NULL) {
				uint8_t event_type = XCB_EVENT_RESPONSE_TYPE(evt);
				switch (event_type) {
					case XCB_KEY_PRESS:
					case XCB_KEY_RELEASE:
					case XCB_BUTTON_PRESS:
					case XCB_BUTTON_RELEASE:
						key_button_event(evt, event_type);
						break;
					case XCB_MOTION_NOTIFY:
						motion_notify(evt, event_type);
						break;
					case XCB_MAPPING_NOTIFY:
						mapping_notify(evt);
						break;
					default:
						PRINTF("received event %u\n", event_type);
						break;
				}
				free(evt);
			}
		}

		if (reload) {
			signal(SIGUSR1, hold);
			reload_cmd();
			reload = false;
		}

		if (bell) {
			signal(SIGALRM, hold);
			abort_chain();
			if (status_fifo != NULL)
				put_status(TIMEOUT_PREFIX, "Timeout reached");
			bell = false;
		}

		if (xcb_connection_has_error(dpy)) {
			warn("The server closed the connection.\n");
			running = false;
		}
	}

	if (redir_fd != -1)
		close(redir_fd);
	if (status_fifo != NULL)
		fclose(status_fifo);
	ungrab();
	cleanup();
	destroy_chord(escape_chord);
	xcb_key_symbols_free(symbols);
	xcb_disconnect(dpy);
	return EXIT_SUCCESS;
}

void key_button_event(xcb_generic_event_t *evt, uint8_t event_type)
{
	xcb_keysym_t keysym = XCB_NO_SYMBOL;
	xcb_button_t button = XCB_NONE;
	bool replay_event = false;
	uint16_t modfield = 0;
	uint16_t lockfield = num_lock | caps_lock | scroll_lock;
	parse_event(evt, event_type, &keysym, &button, &modfield);
	modfield &= ~lockfield & MOD_STATE_FIELD;
	if (keysym != XCB_NO_SYMBOL || button != XCB_NONE) {
		hotkey_t *hk = find_hotkey(keysym, button, modfield, event_type, &replay_event);
		if (hk != NULL) {
			run(hk->command);
			if (status_fifo != NULL)
				put_status(COMMAND_PREFIX, hk->command);
		}
	}
	switch (event_type) {
		case XCB_BUTTON_PRESS:
		case XCB_BUTTON_RELEASE:
			if (replay_event)
				xcb_allow_events(dpy, XCB_ALLOW_REPLAY_POINTER, XCB_CURRENT_TIME);
			else
				xcb_allow_events(dpy, XCB_ALLOW_SYNC_POINTER, XCB_CURRENT_TIME);
			break;
		case XCB_KEY_PRESS:
		case XCB_KEY_RELEASE:
			if (replay_event)
				xcb_allow_events(dpy, XCB_ALLOW_REPLAY_KEYBOARD, XCB_CURRENT_TIME);
			else
				xcb_allow_events(dpy, XCB_ALLOW_SYNC_KEYBOARD, XCB_CURRENT_TIME);
			break;
	}
	xcb_flush(dpy);
}

void motion_notify(xcb_generic_event_t *evt, uint8_t event_type)
{
	xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *) evt;
	/* PRINTF("motion notify %X %X %u\n", e->child, e->detail, e->state); */
	if (motion_interval > 0 && (e->time - last_motion_time) < motion_interval)
		return;
	last_motion_time = e->time;
	uint16_t lockfield = num_lock | caps_lock | scroll_lock;
	uint16_t buttonfield = e->state >> 8;
	uint16_t modfield = e->state & ~lockfield & MOD_STATE_FIELD;
	xcb_button_t button = 1;
	while (~buttonfield & 1 && button < 5) {
		buttonfield = buttonfield >> 1;
		button++;
	}
	hotkey_t *hk = find_hotkey(XCB_NO_SYMBOL, button, modfield, event_type, NULL);
	if (hk != NULL) {
		char command[2 * MAXLEN];
		snprintf(command, sizeof(command), hk->command, e->root_x, e->root_y);
		run(command);
	}
}


void mapping_notify(xcb_generic_event_t *evt)
{
	if (ignore_mapping || !running || chained)
		return;
	xcb_mapping_notify_event_t *e = (xcb_mapping_notify_event_t *) evt;
	PRINTF("mapping notify %u %u\n", e->request, e->count);
	if (e->request == XCB_MAPPING_POINTER)
		return;
	if (xcb_refresh_keyboard_mapping(symbols, e) == 1) {
		destroy_chord(escape_chord);
		get_lock_fields();
		reload_cmd();
		escape_chord = make_chord(ESCAPE_KEYSYM, XCB_NONE, 0, XCB_KEY_PRESS, false, false);
	}
}

void setup(void)
{
	dpy = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(dpy))
		err("Can't open display.\n");
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(dpy)).data;
	if (screen == NULL)
		err("Can't acquire screen.\n");
	root = screen->root;
	if ((shell = getenv(SXHKD_SHELL_ENV)) == NULL && (shell = getenv(SHELL_ENV)) == NULL)
		err("The '%s' environment variable is not defined.\n", SHELL_ENV);
	symbols = xcb_key_symbols_alloc(dpy);
	hotkeys_head = hotkeys_tail = NULL;
	progress[0] = '\0';
}

void cleanup(void)
{
	PUTS("cleanup");
	hotkey_t *hk = hotkeys_head;
	while (hk != NULL) {
		hotkey_t *next = hk->next;
		destroy_chain(hk->chain);
		free(hk->cycle);
		free(hk);
		hk = next;
	}
	hotkeys_head = hotkeys_tail = NULL;
}

void reload_cmd(void)
{
	PUTS("reload");
	cleanup();
	load_config(config_file);
	for (int i = 0; i < num_extra_confs; i++)
		load_config(extra_confs[i]);
	ungrab();
	grab();
}

void hold(int sig)
{
	if (sig == SIGHUP || sig == SIGINT || sig == SIGTERM)
		running = false;
	else if (sig == SIGUSR1)
		reload = true;
	else if (sig == SIGALRM)
		bell = true;
}

void put_status(char c, const char *s)
{
	fprintf(status_fifo, "%c%s\n", c, s);
	fflush(status_fifo);
}
