#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <SDL2/SDL.h>

#define CHUNK_SIZE 16
#define CHUNK_VOLUME (CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE)
#define CHUNK_CENTER ((float) CHUNK_SIZE * 0.5f)

#define WIDTH 320
#define HEIGHT 180
#define WIDTHf (float) WIDTH 
#define HEIGHTf (float) HEIGHT

#define FOV ((float) M_PI * 75.0f / 180.0f)
#define RENDER_DISTANCE 32.0f
#define EPSILON 1e-6f

#define AMBIENT 0.5f

#define SENSITIVITY 0.001f

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
SDL_Texture* texture = NULL;
uint32_t* pixels = NULL;

struct vec2
{
	float x, y;
};

struct vec3
{
	float x, y, z;
};

struct rot
{
	float yaw, pitch;
};

struct cam
{
	struct vec3 pos;
	struct rot rot;
};

struct voxel
{
	bool solid;
	uint8_t r, g, b, a;
};

uint32_t to_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	return
		((uint32_t) r << 24) |
		((uint32_t) g << 16) |
		((uint32_t) b << 8)  |
		((uint32_t) a << 0);
}

const struct vec3 world_up = { 0.0f, 1.0f, 0.0f };

const struct vec3 light_dir = { -0.801784f, 0.534522f, -0.267261f };

struct voxel voxelmap[CHUNK_VOLUME];

struct cam camera =
{
	.pos = { CHUNK_CENTER, (float) CHUNK_SIZE, CHUNK_CENTER },
	.rot = { 0.0f, 0.0f }
};

int voxel_index(int x, int y, int z)
{
	return x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
}

int texture_index(int x, int y)
{
	return x + y * WIDTH;
}

uint8_t rand_uint8()
{
	return (uint8_t) (rand() & 0xFF);
}

void fill_voxels()
{
	for (int z = 0; z < CHUNK_SIZE; z++)
	for (int y = 0; y < CHUNK_SIZE; y++)
	for (int x = 0; x < CHUNK_SIZE; x++)
	{
		voxelmap[voxel_index(x, y, z)] =
		(struct voxel) {
			.solid = (bool) (rand() % 50 == 0),
			.r = (uint8_t) rand_uint8(),
			.g = (uint8_t) rand_uint8(),
			.b = (uint8_t) rand_uint8(),
			.a = (uint8_t) 255
		};
	}
}

void add_floor()
{
	for (int z = 0; z < CHUNK_SIZE; z++)
	for (int y = 0; y < CHUNK_SIZE; y++)
	for (int x = 0; x < CHUNK_SIZE; x++)
	{
		if (y == 0)
		{
			voxelmap[voxel_index(x, y, z)] =
			(struct voxel) {
				.solid = true,
				.r = (uint8_t) 255,
				.g = (uint8_t) 255,
				.b = (uint8_t) 255,
				.a = (uint8_t) 255,
			};
		}
	}
}

struct voxel voxelmap_at(int x, int y, int z)
{
	if ( x < 0 || x >= CHUNK_SIZE ||
		 y < 0 || y >= CHUNK_SIZE ||
		 z < 0 || z >= CHUNK_SIZE )
	{
		return (struct voxel) { false, 0 };
	}
	else
	{
		return voxelmap[voxel_index(x, y, z)];
	}
}

struct vec3 position(struct vec3 origin, struct vec3 dir, float len)
{
	return (struct vec3)
	{
		origin.x + dir.x * len,
		origin.y + dir.y * len,
		origin.z + dir.z * len
	};
}

struct vec3 direction(float yaw, float pitch)
{
	return (struct vec3)
	{
		cos(pitch) * cos(yaw),
		sin(pitch),
		cos(pitch) * sin(yaw)
	};
}

struct vec3 cross(struct vec3 a, struct vec3 b)
{
	return (struct vec3)
	{
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
}

struct vec3 unit(struct vec3 dir)
{
	const float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	return (struct vec3)
	{
		dir.x / len,
		dir.y / len,
		dir.z / len
	};
}

float dot(struct vec3 a, struct vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

int signf(float val)
{
	return (int) (val > 0.0f) - (val < 0.0f);
}

float safe_zero(float val)
{
	return (val != 0.0f) ? val : EPSILON;
}

struct ray
{
	bool hit;
	float len;
	struct vec3 norm;
	struct vec3 pos;
};

struct ray raycast(struct vec3 origin, struct vec3 dir)
{
	int x = (int) origin.x;
	int y = (int) origin.y;
	int z = (int) origin.z;

	const float xf = (float) x;
	const float yf = (float) y;
	const float zf = (float) z;

	const int x_step = signf(dir.x);
	const int y_step = signf(dir.y);
	const int z_step = signf(dir.z);

	const float safe_dirx = safe_zero(dir.x);
	const float safe_diry = safe_zero(dir.y);
	const float safe_dirz = safe_zero(dir.z);

	const float x_delta = fabsf(1.0f / safe_dirx);
	const float y_delta = fabsf(1.0f / safe_diry);
	const float z_delta = fabsf(1.0f / safe_dirz);

	float x_max = (x_step > 0)
		? (xf + 1.0f - origin.x) / safe_dirx
		: (origin.x - xf) / -safe_dirx;
	float y_max = (y_step > 0)
		? (yf + 1.0f - origin.y) / safe_diry
		: (origin.y - yf) / -safe_diry;
	float z_max = (z_step > 0)
		? (zf + 1.0f - origin.z) / safe_dirz
		: (origin.z - zf) / -safe_dirz;

	float distance = 0.0f;
	struct vec3 normal;

	while (distance < RENDER_DISTANCE)
	{
		if (voxelmap_at(x, y, z).solid)
		{
			return (struct ray)
			{
				.hit = true,
				.len = distance,
				.norm = normal,
				.pos = (struct vec3) { x, y, z }
			};
		}
		
		if (x_max < y_max && x_max < z_max)
		{
			x += x_step;
			distance = x_max;
			x_max += x_delta;
			normal = (struct vec3) { -x_step, 0.0f, 0.0f };
		}

		else if (y_max < z_max)
		{
			y += y_step;
			distance = y_max;
			y_max += y_delta;
			normal = (struct vec3) { 0.0f, -y_step, 0.0f };
		}
		else
		{
			z += z_step;
			distance = z_max;
			z_max += z_delta;
			normal = (struct vec3) { 0.0f, 0.0f, -z_step };
		}
	}

	return (struct ray) { .hit = false };
}

struct vec2 ndc(int x, int y)
{
	const float xf = (float) x;
    const float yf = (float) y;

	const float widthf = (float) WIDTH;
	const float heightf = (float) HEIGHT;

    const float aspect = widthf / heightf;
    const float scale = tanf(FOV / 2.0f);

    const float ndc_x = (2.0f * (xf + 0.5f) / widthf - 1.0f) * scale * aspect;
    const float ndc_y = (1.0f - 2.0f * (yf + 0.5f) / heightf) * scale;

    return (struct vec2) { ndc_x, ndc_y };
}

void draw_pixel(int x, int y, struct vec3 dir)
{
	const struct ray r = raycast(camera.pos, dir);

	if (r.hit == 0)
	{
		pixels[texture_index(x, y)] = 0x000000FF;
		return;
	}

	const struct vec3 surface_pos = position(camera.pos, dir, r.len);
	const float bias = 0.001;
	const struct vec3 shadow_origin =
	{
		surface_pos.x + light_dir.x * bias,
		surface_pos.y + light_dir.y * bias,
		surface_pos.z + light_dir.z * bias
	};

	const struct ray l = raycast(shadow_origin, light_dir);

	float brightness = AMBIENT;

	if (l.hit == false)
	{
		// Lambertian shading
		const float angle = fmaxf(dot(r.norm, light_dir), 0);
		brightness = AMBIENT + (1 - AMBIENT) * angle;
	}

	const struct voxel v = voxelmap[voxel_index(r.pos.x, r.pos.y, r.pos.z)];
	pixels[texture_index(x, y)] = to_rgba(v.r * brightness, v.g * brightness, v.b * brightness, v.a);
}

void draw_frame()
{
	const struct vec3 forward = direction(camera.rot.yaw, camera.rot.pitch);
	const struct vec3 right = unit(cross(forward, world_up));
	const struct vec3 camera_up = unit(cross(right, forward));

	for (int y = 0; y < HEIGHT; y++)
	for (int x = 0; x < WIDTH; x++)
	{
		const struct vec2 rtn = ndc(x, y);
		const struct vec3 dir = unit((struct vec3)
		{
			forward.x + right.x * rtn.x + camera_up.x * rtn.y,
			forward.y + right.y * rtn.x + camera_up.y * rtn.y,
			forward.z + right.z * rtn.x + camera_up.z * rtn.y
		});

		draw_pixel(x, y, dir);
	}

	SDL_UpdateTexture(texture, NULL, pixels, WIDTH * sizeof(uint32_t));
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}


// IO

const float MOVE_SPEED = 0.2;

void move_w()
{
	const struct vec3 dir = direction(camera.rot.yaw, camera.rot.pitch);
	camera.pos.x += dir.x * MOVE_SPEED;
	camera.pos.y += dir.y * MOVE_SPEED;
	camera.pos.z += dir.z * MOVE_SPEED;
}

void move_s()
{
	const struct vec3 dir = direction(camera.rot.yaw, camera.rot.pitch);
	camera.pos.x -= dir.x * MOVE_SPEED;
	camera.pos.y -= dir.y * MOVE_SPEED;
	camera.pos.z -= dir.z * MOVE_SPEED;
}

void move_a()
{
	const struct vec3 dir = unit(cross(direction(camera.rot.yaw, camera.rot.pitch), world_up));
	camera.pos.x -= dir.x * MOVE_SPEED;
	camera.pos.y -= dir.y * MOVE_SPEED;
	camera.pos.z -= dir.z * MOVE_SPEED;
}

void move_d()
{
	const struct vec3 dir = unit(cross(direction(camera.rot.yaw, camera.rot.pitch), world_up));
	camera.pos.x += dir.x * MOVE_SPEED;
	camera.pos.y += dir.y * MOVE_SPEED;
	camera.pos.z += dir.z * MOVE_SPEED;
}

void move_space()
{
	camera.pos.y += MOVE_SPEED;
}

void move_shift()
{
	camera.pos.y -= MOVE_SPEED;
}

void rot_camera_dx(int dx)
{
	camera.rot.yaw += (float) dx * SENSITIVITY;
}

void rot_camera_dy(int dy)
{
	camera.rot.pitch -= (float) dy * SENSITIVITY;
	if (camera.rot.pitch > 1.5f) camera.rot.pitch = 1.5f;
	if (camera.rot.pitch < -1.5f) camera.rot.pitch = -1.5f;
}

int print_SDL_Error(int val)
{
	printf("SDL_Error (%d): %s\n", val, SDL_GetError());
	return 1;
}

int init_SDL()
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
		return print_SDL_Error(0);

	window = SDL_CreateWindow(
		"Voxel Raytracer",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		WIDTH, HEIGHT,
		SDL_WINDOW_SHOWN
	);

	if (!window) return print_SDL_Error(1);

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

	if (!renderer) return print_SDL_Error(2);

	texture = SDL_CreateTexture(
		renderer,
		SDL_PIXELFORMAT_RGBA8888,
		SDL_TEXTUREACCESS_STREAMING,
		WIDTH, HEIGHT
	);

	if (!texture) return print_SDL_Error(3);

	pixels = malloc(WIDTH * HEIGHT * sizeof(uint32_t));

	SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
	SDL_SetRelativeMouseMode(SDL_TRUE);

	return 0;
}

void end_SDL()
{
	free(pixels);
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

struct input_state
{
	bool w, a, s, d;
	bool space, shift, ctrl;
	int mouse_dx, mouse_dy;
};

void controls(struct input_state* input)
{
	if (input->w) move_w();
	if (input->s) move_s();
	if (input->a) move_a();
	if (input->d) move_d();
	if (input->space) move_space();
	if (input->shift) move_shift();
	if (input->mouse_dx) rot_camera_dx(input->mouse_dx);
	if (input->mouse_dy) rot_camera_dy(input->mouse_dy);
}

void loop()
{
	struct input_state input = { 0 };

	bool running = true;

	SDL_Event event;

	while (running)
	{
		input.mouse_dx = 0;
		input.mouse_dy = 0;

		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
				case SDL_QUIT:
					running = false;
					break;

				case SDL_KEYDOWN:
				case SDL_KEYUP:
					{
						bool is_down = (event.type == SDL_KEYDOWN);
						SDL_Keycode key = event.key.keysym.sym;

						switch (key)
						{
							case SDLK_ESCAPE:
								running = false;
								break;

							case SDLK_w:
								input.w = is_down;
								break;

							case SDLK_a:
								input.a = is_down;
								break;

							case SDLK_s:
								input.s = is_down;
								break;

							case SDLK_d:
								input.d = is_down;
								break;

							case SDLK_SPACE:
								input.space = is_down;
								break;

							case SDLK_LSHIFT:
								input.shift = is_down;
								break;

							case SDLK_LCTRL:
								input.ctrl = is_down;
								break;
						}
					}
					break;

					case SDL_MOUSEMOTION:
						input.mouse_dx += event.motion.xrel;
						input.mouse_dy += event.motion.yrel;
						break;
			}
		}

		controls(&input);

		draw_frame();

		SDL_RenderPresent(renderer);

		SDL_Delay(8);
	}
}

int main(int argc, char* argv[])
{
	if (init_SDL()) return 1;

	fill_voxels(); // init voxelmap
	add_floor();

	loop();

	end_SDL();

	return 0;
}
