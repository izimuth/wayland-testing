
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <syscall.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-client.h>

#include "xdg-shell.h"

wl_display *display = NULL;
wl_compositor *compositor = NULL;
wl_surface *surface = NULL;
zxdg_surface_v6 *xdg_surface = NULL;
zxdg_shell_v6 *shell = NULL;
wl_shell_surface *shell_surface = NULL;
wl_shm *shm = NULL;

void global_registry_handler(void *data, wl_registry *registry, uint32_t id, const char *interface, uint32_t version)
{
	printf("Got a registry event for %s (object id %d) (version %d)\n", interface, id, version);

	if (strcmp(interface, "wl_compositor") == 0) 
	{
		compositor = (wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
	}
	else if (strcmp(interface, "zxdg_shell_v6") == 0) 
	{
		shell = (zxdg_shell_v6*)wl_registry_bind(registry, id, &zxdg_shell_v6_interface, 1);
	}
	else if (strcmp(interface, "wl_shm") == 0)
	{
		shm = (wl_shm*)wl_registry_bind(registry, id, &wl_shm_interface, 1);
	}
}

void global_registry_remover(void *data, wl_registry *registry, uint32_t id)
{
	printf("Got a registry losing event for %d\n", id);
}

const wl_registry_listener registry_listener = {
	global_registry_handler, 
	global_registry_remover
};

void finished(int code)
{
	if (surface)
	{
		wl_surface_destroy(surface);
	}

	if (display)
	{
		printf("Disconnecting from display\n");
		wl_display_disconnect(display);
	}

	if (code != 0)
		exit(code);
}

void xdg_toplevel_config_handler(void *data, zxdg_toplevel_v6 *target, int32_t w, int32_t h, wl_array *states)
{
	printf("configure: %d x %d\n", w, h);
}

void xdg_toplevel_close_handler(void *data, zxdg_toplevel_v6 *target)
{
	printf("Close\n");
}

zxdg_toplevel_v6_listener toplevel_listener = {
	.configure = xdg_toplevel_config_handler,
	.close = xdg_toplevel_close_handler
};

void xdg_surface_config_handler(void *data, zxdg_surface_v6 *surface, uint32_t serial)
{
	zxdg_surface_v6_ack_configure(surface, serial);
}

zxdg_surface_v6_listener surface_listener = {
	.configure = xdg_surface_config_handler
};

int main(int argc, char **argv)
{
	display = wl_display_connect(NULL);
	
	if (display == NULL)
	{
		printf("Can't connect to display\n");
		exit(1);
	}

	printf("Connected to display!\n");

	wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);

	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (compositor == NULL)
	{
		printf("Can't find compositor!\n");
		finished(1);
	}

	if (shell == NULL)
	{
		printf("Can't find shell\n");
		finished(1);
	}

	surface = wl_compositor_create_surface(compositor);
	if (surface == NULL)
	{
		printf("Failed to create surface!\n");
		finished(1);
	}

	xdg_surface = zxdg_shell_v6_get_xdg_surface(shell, surface);
	if (xdg_surface == NULL)
	{
		printf("Failed to get xdg surface!\n");
		finished(1);
	}

	zxdg_surface_v6_add_listener(xdg_surface, &surface_listener, NULL);

	zxdg_toplevel_v6 *toplevel = zxdg_surface_v6_get_toplevel(xdg_surface);
	
	zxdg_toplevel_v6_add_listener(toplevel, &toplevel_listener, NULL);

	wl_surface_commit(surface);

	// create anonymous file
	int width = 1280;
	int height = 720;
	int stride = width * 4;
	int size = stride * height;
	int fd = syscall(SYS_memfd_create, "buffer", 0);

	// set its size
	ftruncate(fd, size);

	// map it to a pointer
	uint8_t *data = (uint8_t *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	// create a memory pool
	wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);

	// allocate buffer in pool
	wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);

	wl_display_roundtrip(display);

	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);

	while(1) 
	{
		wl_display_dispatch(display);
	}

	finished(0);
	return 0;
}