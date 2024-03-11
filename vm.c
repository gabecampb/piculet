/*
Copyright (c) 2021-2024 Gabriel Campbell

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// vm.c
// Gabriel Campbell (github.com/gabecampb)
// Created 2020-03-28

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

uint8_t* root_path = "/tmp";		// full path to some directory

// for file I/O functions
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include <GLFW/glfw3.h>

typedef struct dirent dirent;

uint8_t show_program_info, show_about, enable_vsync;

#define SHOW_FPS 1 /* show FPS counter in window title */
#define SHOW_INS_OUT_OF_RANGE 0	/* print when a thread is killed due to fetching instruction out of instruction range */
#define SHOW_NEW_THREAD 0 /* print init PC and thread ID for any newly created threads */
#define SHOW_SEGFAULT 0	/* print message at segfault */
#define SHOW_SHADERS 0	/* print GLSL shaders */

#define BUILD_VER 1	/* current build version */
#define SLEEP_AT_SWAP 0	 /* force CPU sleep at buffer swap; only for testing */
#define SLEEP_SWAP_MS 16 /* how long to (force) sleep at buffer swap, in ms */
#define THR_0_RESTRICT_INS_RANGE 0 /* force thread 0 to have instruction memory range that spans only the boot-loaded program instead of entire main memory; only for testing */

#define STD_OUTPUT 1 /* whether or not to allow using the standard output register to print to console */
const char* WINDOW_TITLE = "Piculet VM";

uint32_t window_width = 500;
uint32_t window_height = 340;
int32_t cursor_x = 0;
int32_t cursor_y = 0;
uint8_t mouse_buttons = 0;
double scroll_x = 0, scroll_y = 0;
uint8_t kbd_states[9];

#define SIZE_MAIN_MEM (512*1000000) /* 512 MB */
#define SIZE_SYS_MEM (25*1000000) /* 25 MB */
#define HW_INFORMATION (SIZE_MAIN_MEM+18*1000)	/* HARDWARE INFORMATION STARTS 18 MB INTO SYSTEM MEMORY */
#define HW_INFO_HIGH (SIZE_MAIN_MEM+20*1000) /* LAST ADDRESS OF HARDWARE INFORMATION */

uint32_t max_texture_size = 1024;	// max texture dimensions
uint8_t gl_finish;	// whether or not to call glFinish() after all threads have cycled; set back to 0 after all threads have finished a cycle
uint8_t gl_swap;	// whether or not to swap the buffers after all threads have cycled; set back to 0 after all threads have finished a cycle

struct timespec start_tm;
#define NS_PER_SEC 1000000000

uint8_t memory[SIZE_MAIN_MEM+SIZE_SYS_MEM];
uint64_t mappings_low = HW_INFORMATION;	// the lowest address for current buffer mappings; starts at beginning of HW information and is subtracted as buffers are mapped

typedef struct map_t { uint64_t address, size, privacy_key; } map_t;
map_t* mappings;			// mapping regions
uint64_t n_mappings;		// number of mapping regions

typedef struct thread_t thread_t;
thread_t* threads;
uint32_t n_threads;

typedef struct object_t object_t;
object_t* objects;
uint64_t n_objects = 0;

#define MAX_NUMBER_BOUND_SETS 4 /* maximum number of descriptor sets */
uint32_t max_number_ubos = 100;		// maximum number of uniform buffers accessible by a pipeline
uint32_t max_number_sbos = 100;		// maximum number of storage buffers accessible by a pipeline
uint32_t max_number_samplers = 8;	// maximum number of samplers accessible by a pipeline
uint32_t max_number_images = 8;		// maximum number of images accessible by a pipeline
uint32_t max_number_as = 0;			// maximum number of acceleration structures accessible by a pipeline (0; RT is unsupported currently)

typedef struct segtable_t segtable_t;
typedef struct object_t object_t;

#define TYPE_CBO 0x00
#define TYPE_VAO 0x01
#define TYPE_VBO 0x02
#define TYPE_IBO 0x03
#define TYPE_TBO 0x04
#define TYPE_FBO 0x05
#define TYPE_UBO 0x06
#define TYPE_SBO 0x07
#define TYPE_TLAS 0x08
#define TYPE_BLAS 0x09
#define TYPE_DBO 0x0A
#define TYPE_SBT 0x0B
#define TYPE_SAMPLER_DESC 0x0C
#define TYPE_IMAGE_DESC 0x0D
#define TYPE_UNIFORM_DESC 0x0E
#define TYPE_STORAGE_DESC 0x0F
#define TYPE_AS_DESC 0x10
#define TYPE_DSET 0x11
#define TYPE_SET_LAYOUT 0x12
#define TYPE_VSH 0x13
#define TYPE_PSH 0x14
#define TYPE_RGENSH 0x15
#define TYPE_AHITSH 0x16
#define TYPE_CHITSH 0x17
#define TYPE_MISSSH 0x18
#define TYPE_CSH 0x19
#define TYPE_RASTER_PIPE 0x1A
#define TYPE_RT_PIPE 0x1B
#define TYPE_COMPUTE_PIPE 0x1C
#define TYPE_AUD_DATA 0x1D
#define TYPE_AUD_SRC 0x1E
#define TYPE_AUD_LIS 0x1F
#define TYPE_AUD_OCC 0x20
#define TYPE_VID_DATA 0x21
#define TYPE_SCKT 0x22
#define TYPE_SEGTABLE 0x23

#define UNIFORM_DESC_BINDING 0x00
#define STORAGE_DESC_BINDING 0x01
#define SAMPLER_DESC_BINDING 0x02
#define IMAGE_DESC_BINDING 0x03
#define AS_DESC_BINDING 0x04
#define DESC_SET_BINDING 0x05
#define SET_LAYOUT_BINDING 0x06
#define VAO_BINDING 0x07
#define VBO_BINDING 0x08
#define IBO_BINDING 0x09
#define TBO_BINDING 0x0A
#define CBO_BINDING 0x0B
#define UBO_BINDING 0x0C
#define SBO_BINDING 0x0D
#define TLAS_BINDING 0x0E
#define BLAS_BINDING 0x0F
#define DBO_BINDING 0x10
#define SBT_BINDING 0x11
#define SHADER_BINDING 0x12
#define PIPELINE_BINDING 0x13
#define FBO_BINDING 0x14
#define AUD_DATA_BINDING 0x15
#define AUD_SRC_BINDING 0x16
#define AUD_LIS_BINDING 0x17
#define AUD_OCC_BINDING 0x18
#define VID_DATA_BINDING 0x19
#define SEGTABLE_BINDING 0x20
#define N_BINDINGS 27

#define SR_BIT_N 0x400000000000
#define SR_BIT_Z 0x200000000000
#define SR_BIT_C 0x100000000000
#define SR_BIT_V 0x80000000000
#define SR_BIT_SEGFAULT 0x800000000000

typedef struct object_bindings_t {
	uint64_t uniform_desc_binding;
	uint64_t storage_desc_binding;
	uint64_t sampler_desc_binding;
	uint64_t image_desc_binding;
	uint64_t as_desc_binding;
	uint64_t desc_set_binding;
	uint64_t set_layout_binding;
	uint64_t vao_binding;
	uint64_t vbo_binding;
	uint64_t ibo_binding;
	uint64_t tbo_binding;
	uint64_t cbo_binding;
	uint64_t ubo_binding;
	uint64_t sbo_binding;
	uint64_t tlas_binding;
	uint64_t blas_binding;
	uint64_t dbo_binding;
	uint64_t sbt_binding;
	uint64_t shader_binding;
	uint64_t pipeline_binding;
	uint64_t fbo_binding;
	uint64_t aud_data_binding;
	uint64_t aud_src_binding;
	uint64_t aud_lis_binding;
	uint64_t aud_occ_binding;
	uint64_t vid_data_binding;
	uint64_t segtable_binding;
} object_bindings_t;

typedef struct thread_t {
	uint64_t id;
	uint64_t* primary;
	uint64_t* secondary;
	uint64_t* output;
	uint64_t* regs;
	uint64_t instruction_max, instruction_min;	// range for executable instructions in main memory
	uint8_t end_cyc;	// used in cycle execution
	uint64_t parent, n_descendants;	// used in determining where ...
	uint64_t* descendants;			// 	this thread sits in the hierarchy; lists only direct descendants
	uint8_t killed;	// whether or not this thread was killed
	uint8_t detached;	// whether or not this thread is detached
	uint64_t joining;	// what thread this thread is waiting for to be killed (0 if none)
	uint8_t perm_screenshot, perm_camera, perm_microphones, perm_networking, perm_file_io, perm_thread_creation;	// whether or not this thread has these permissions
	uint8_t* highest_dir;	// the highest accessible path for this thread
	uint8_t highest_dir_length;	// the length of this thread's highest accessible path string (in bytes, incl. null character)
	object_bindings_t bindings;	// this thread's object bindings

	uint8_t object_privacy;		// whether or not this thread has object privacy enabled
	uint64_t privacy_key;		// this thread's object privacy key

	uint64_t* created_threads;	// the IDs of threads created by this thread during a cycle (they will exist but be in killed state until this thread cycles again)
	uint32_t n_created_threads;	// the count of threads created by this thread during a cycle

	uint64_t sleep_start_ns;	// time that the thread was put to sleep
	uint64_t sleep_duration_ns;	// time that the thread was put to sleep for

	uint64_t segtable_id;

	FILE* file_streams[65534];	// open file streams (ID 1-65535)
} thread_t;

// create new mapping region in system memory with specified size and object privacy key, then return address
uint64_t new_mapping(uint64_t privacy_key, uint64_t size) {
	mappings_low -= size;
	mappings = realloc(mappings, sizeof(map_t)*(n_mappings+1));
	mappings[n_mappings].address = mappings_low;
	mappings[n_mappings].size = size;
	mappings[n_mappings].privacy_key = privacy_key;
	n_mappings++;
	return mappings_low;
}

// delete a mapping region in system memory that starts at specified address
void delete_mapping(uint64_t address) {
	for(uint32_t i = 0; i < n_mappings; i++)
		if(address == mappings[i].address) {
			if(address == mappings_low) mappings_low += mappings[i].size;
			if(i!=n_mappings-1) mappings[i] = mappings[n_mappings-1];
			mappings = realloc(mappings, sizeof(map_t)*(n_mappings-1));
			n_mappings--;
			if(!n_mappings) mappings_low = HW_INFORMATION;
			return;
		}
}

typedef struct segment_t {
	uint64_t v_address;
	uint64_t p_address;
	uint64_t length;
	uint8_t deleted;
} segment_t;

typedef struct segtable_t {
	segment_t* segments;
	uint32_t n_segments;
} segtable_t;

// add segment to a segment table
uint64_t add_segment(segtable_t* segtable, segment_t new_segment) {
	for(uint32_t i = 0; i < segtable->n_segments; i++)
		if(segtable->segments[i].deleted) {
			segtable->segments[i] = new_segment;
			return i;
		}
	segtable->segments = realloc(segtable->segments, sizeof(segment_t)*(segtable->n_segments+1));
	memcpy(&segtable->segments[segtable->n_segments], &new_segment, sizeof(segment_t));
	segtable->n_segments++;
	return segtable->n_segments-1;
}

// reset segments
void reset_segtable(segtable_t* segtable) {
	if(segtable->segments) free(segtable->segments);
	segtable->segments = 0;
	segtable->n_segments = 0;
}

uint8_t check_hwinfo(uint64_t address, uint64_t size) {
	return address >= HW_INFORMATION && address + size - 1 <= HW_INFO_HIGH;
}

uint8_t check_mapped_region(uint64_t privacy_key, uint64_t address, uint64_t size) {
	for(uint32_t i = 0; i < n_mappings; i++)
		if(privacy_key == mappings[i].privacy_key && address >= mappings[i].address && address + size - 1 < mappings[i].address + mappings[i].size) return 1;
	return 0;	// not part of mapped region
}

void update_hwinfo() {
	uint8_t* hwi = &memory[HW_INFORMATION];
	*(uint32_t*)hwi = 0x180;	// hw support info; texture filtering + hw accel gfx
	hwi[4] = 0;		// 1 display
	*(uint64_t*)(hwi+5) = HW_INFORMATION+500; // address to dimensions of each display
	*(uint64_t*)(hwi+13) = 0; // address to 16-bit touch count for each display
	*(uint64_t*)(hwi+21) = 0; // address to 16-bit current touch count for each display
	*(uint64_t*)(hwi+29) = 0; // address to touch coordinates
	*(uint32_t*)(hwi+37) = 1; // current # of cursors
	*(uint64_t*)(hwi+41) = HW_INFORMATION+600; // address to 32-bit cursor coordinates
	*(uint64_t*)(hwi+49) = HW_INFORMATION+700; // address to 8-bit additional cursor inputs
	hwi[57] = 1; // number of connected keyboards
	*(uint64_t*)(hwi+58) = HW_INFORMATION+800; // address to keyboard info
	hwi[66] = 0; // number of connected controllers
	hwi[67] = 0; // number of controller buttons
	hwi[68] = 0; // number of controller axes
	hwi[69] = 0; // number of controller positions
	hwi[70] = 0; // number of controller orientations
	*(uint64_t*)(hwi+71) = 0; // address to controller info
	hwi[79] = 0; // number of microphones
	hwi[80] = 0; // number of available cameras
	*(uint64_t*)(hwi+81) = 0; // address of camera image dimensions
	*(uint32_t*)(hwi+89) = 0; // min cpu mhz
	*(uint32_t*)(hwi+93) = 0; // max cpu mhz
	*(uint64_t*)(hwi+97) = 0; // address to current clock speed for each core
	*(uint16_t*)(hwi+105) = 0; // number of cpu cores
	*(uint64_t*)(hwi+107) = SIZE_MAIN_MEM; // capacity of main memory
	*(uint64_t*)(hwi+115) = 0; // gpu mem capacity
	*(uint64_t*)(hwi+123) = 0; // gpu mem available
	*(uint32_t*)(hwi+131) = 100.f; // battery percent
	*(uint16_t*)(hwi+135) = 0; // num of graphics queues (0 corresponds to 1)
	*(uint16_t*)(hwi+137) = 0; // num of compute queues (0 corresponds to 1)
	*(uint32_t*)(hwi+139) = UINT32_MAX; // max desc binding points per descriptor set
	*(uint32_t*)(hwi+143) = 16; // num of audio occlusion geometry binding points per audio listener
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
	*(uint32_t*)(hwi+147) = max_texture_size; // max texture size
	*(uint32_t*)(hwi+151) = 1024; // max local work-group size X
	*(uint32_t*)(hwi+155) = 1024; // max local work-group size Y
	*(uint32_t*)(hwi+159) = 64; // max local work-group size Z
	*(uint32_t*)(hwi+163) = 1024; // max local work-group size
	*(uint32_t*)(hwi+167) = 65535; // max global work-group size X
	*(uint32_t*)(hwi+171) = 65535; // max global work-group size Y
	*(uint32_t*)(hwi+175) = 65535; // max global work-group size Z
	*(uint32_t*)(hwi+179) = 0; // max RT recursion
	*(uint32_t*)(hwi+183) = 0; // max geometries in BLAS
	*(uint32_t*)(hwi+187) = 0; // max count of geom instances in TLAS
	*(uint32_t*)(hwi+191) = 0; // max total count of triangles in TLAS
	hwi[195] = MAX_NUMBER_BOUND_SETS-1; // max number of accessible desc sets in a pipeline
	*(uint64_t*)(hwi+196) = max_number_as; // max AS descriptors in a pipeline
	*(uint64_t*)(hwi+204) = max_number_samplers;
	*(uint64_t*)(hwi+212) = max_number_images;
	*(uint64_t*)(hwi+220) = max_number_ubos;
	*(uint64_t*)(hwi+228) = max_number_sbos;
	*(uint64_t*)(hwi+236) = 0; // address to supported audio formats
	*(uint64_t*)(hwi+244) = HW_INFORMATION+900; // address to supported video/image formats
	*(uint16_t*)(hwi+252) = 0; // max num of audio channels

	*(uint32_t*)(hwi+500) = window_width;
	*(uint32_t*)(hwi+504) = window_height;
	*(int32_t*)(hwi+600) = cursor_x;
	*(int32_t*)(hwi+604) = cursor_y;
	hwi[700] = mouse_buttons;
	*(int32_t*)(hwi+701) = scroll_x;
	*(int32_t*)(hwi+705) = scroll_y;
	memmove(&hwi[800], &kbd_states, 9);

	strcpy(&hwi[900], "png,jpg,jpeg");
}

uint8_t check_sys_region(uint64_t privacy_key, uint64_t address, uint64_t size) {
	if(check_hwinfo(address,size)) {
		glfwPollEvents();
		update_hwinfo();
	}
	return check_hwinfo(address,size) || check_mapped_region(privacy_key,address,size); 
}

void init_threads() {	// creates thread 0
	threads = calloc(1, sizeof(thread_t));	// initialize the thread hierarchy (calloc to init all bits to 0)
	threads[0].regs = calloc(16, sizeof(uint64_t));
	threads[0].instruction_max = SIZE_MAIN_MEM - 1;
	threads[0].perm_screenshot = 1;
	threads[0].perm_camera = 1;
	threads[0].perm_microphones = 1;
	threads[0].perm_networking = 1;
	threads[0].perm_file_io = 1;
	threads[0].perm_thread_creation = 1;
	threads[0].highest_dir = malloc(1);
	threads[0].highest_dir[0] = '/';
	threads[0].highest_dir_length = 1;
	memset(&threads[0].bindings, 0, sizeof(object_bindings_t));
	threads[0].primary = &threads[0].regs[0];
	threads[0].secondary = &threads[0].regs[0];
	threads[0].output = &threads[0].regs[0];
	n_threads = 1;
}

// create a new thread: does not set anything for new thread but its parent and adds the new thread to descendants array in parent
uint64_t new_thread(uint64_t parent_id) {
	threads = realloc(threads, sizeof(thread_t)*(n_threads+1));	// add thread to the thread hierarchy
	thread_t* parent = &threads[parent_id];
	thread_t* thread = &threads[n_threads];
	thread->regs = calloc(16, sizeof(uint64_t));
	thread->parent = parent->id;
	thread->id = n_threads;
	memset(&thread->bindings, 0, sizeof(object_bindings_t));
	thread->end_cyc = 0;
	thread->n_descendants = 0;
	thread->detached = 0;
	thread->joining = 0;
	thread->killed = 1;	// this is set to 0 at the next cycle of parent (the thread is created as killed in order to treat the thread as non-existent until then)
	thread->sleep_start_ns = 0;
	thread->sleep_duration_ns = 0;
	thread->primary = &thread->regs[0];
	thread->secondary = &thread->regs[0];
	thread->output = &thread->regs[0];

	// push the created thread's ID to the back of the parent's created_threads array
	parent->created_threads = realloc(parent->created_threads, sizeof(uint64_t)*(parent->n_created_threads+1));
	parent->created_threads[parent->n_created_threads] = thread->id;
	parent->n_created_threads++;

	// push current n_threads to the back of the parent's descendants array
	parent->descendants = realloc(parent->descendants, sizeof(uint64_t)*(parent->n_descendants+1));
	parent->descendants[parent->n_descendants] = n_threads;
	parent->n_descendants++;

	n_threads++;
	return n_threads-1;
}

void kill_thread(thread_t* thread) {
	thread->killed = 1;
	for(uint32_t i = 0; i < thread->n_descendants; i++)
		threads[thread->descendants[i]].regs[13] |= 0x10000; // set the parent thread killed SR bit for this descendant
	free(thread->highest_dir);

	// to do: free the memory allocated for all threads whose IDs ares named in the thread->created_threads array
}

// returns 1 if child is a descendant of parent, 0 otherwise
uint8_t check_descendant(thread_t* parent, thread_t* child) {
	if(parent->id == 0) {
		if(child->id != 0) return 1; // all child are children of thread 0
		else return 0; // thread 0 not a child of thread 0
	}
	while(1) {
		child = &threads[child->parent];
		if(child == parent)  return 1;
		if(child->parent == 0) return 0;
	}
}

int64_t abs64(int64_t x) {
	if(x < 0) return x + x*2;
	return x;
}

// returns whether or not an addition of two 32-bit signed integers will result in overflow
uint8_t check_overflow32(int32_t a, int32_t b) {
	int32_t res = a+b;
	if(a > 0 && b > 0 && res < 0) return 1;
	if(a < 0 && b < 0 && res > 0) return 1;
	return 0;
}

// returns whether or not an addition of two 64-bit signed integers will result in overflow
uint8_t check_overflow64(int64_t a, int64_t b) {
	int64_t res = a+b;
	if(a > 0 && b > 0 && res < 0) return 1;
	if(a < 0 && b < 0 && res > 0) return 1;
	return 0;
}

typedef struct sbo_t {
	void* data;
	uint64_t size;
} sbo_t;

typedef struct desc_binding_t {
	uint32_t binding_number;// the binding number of this descriptor binding
	uint8_t binding_type;	// the type of this descriptor binding (0=uniform, 1=storage, 2=sampler, 3=image, 4=AS)
	uint32_t* object_ids;   // IDs of objects referenced in this binding (only sampler bindings can have multiple)
	uint8_t* min_filters;	// one for each sampler descriptor
	uint8_t* mag_filters;	// one for each sampler descriptor
	uint8_t* s_modes;		// one for each sampler descriptor
	uint8_t* t_modes;		// one for each sampler descriptor
	uint16_t n_descs;		// number of descriptors at this binding
} desc_binding_t;

typedef struct set_layout_t {
	uint32_t* binding_numbers;	// contains the binding point number for each descriptor binding
	uint8_t* binding_types;		// contains binding types for each of the binding points named in 'binding_numbers' array (desc type; 0=uniform, 1=storage, 2=sampler, 3=image, 4=AS)
	uint16_t* n_descs;			// number of descriptors in each binding point
	uint32_t n_binding_points;	// number of descriptor binding points, 0 corresponds to 1
} set_layout_t;

typedef struct desc_set_t {
	desc_binding_t* bindings;	// bindings for this descriptor set
	uint32_t n_bindings;		// number of bindings in this descriptor set, 0 corresponds to 1
	uint32_t layout_id;
} desc_set_t;

// structure for shader bytecode (in shader objects), or translated GLSL source code (in create_pipeline)
typedef struct shader_t {
	char* src;      // shader source bytecode, or GLSL source code
	uint64_t size;  // length in bytes (incl. null character)
	uint8_t type;    // 0 = vertex, 1 = pixel, 2 = compute
} shader_t;

typedef struct cbo_t {          // command buffer structure
	uint64_t bindings[4];	// the current bindings for the command buffer (arranged in order specified under Graphics States; these also affect recorded commands)
		// bindings are bound object IDs for the command buffer: bindings[0] = pipeline object, bindings[1] = FBO, bindings[2] = VBO, bindings[3] = IBO
	uint32_t dset_ids[MAX_NUMBER_BOUND_SETS];	// the descriptor sets bound to this command buffer
	uint8_t pipeline_type;	// set after initialization or after command buffer reset at first pipeline bound to CBO; the type of pipeline this CBO uses. initialized to 2 (none bound).
    void* cmds;	// the command opcodes, alongside the information affecting the commands execution as they were when the command was issued. see record_command() for more information
    uint64_t size;	// size of cmds
} cbo_t;

typedef struct definition_t definition_t;

typedef struct pipeline_t {
	GLint gl_program;		// this pipeline's GL program
	uint64_t vao_id;			// the ID of the VAO object for this pipeline (rasterization pipeline only)

	uint32_t dset_layout_ids[MAX_NUMBER_BOUND_SETS];	// IDs of descriptor set layout objects referenced for each set binding
	uint16_t n_desc_sets;	// number of enabled descriptor sets (0-4 for rasterization and compute, 0-1 for RT pipelines)
	uint8_t type;		// 0 = rasterization, 1 = ray tracing, 2 = compute

	definition_t* defs_1; // information about definitions present in the vertex/compute shader
	definition_t* defs_2; // information about definitions present in the pixel shader
	uint32_t n_defs_1;
	uint32_t n_defs_2;

	uint8_t* push_constant_data;
	uint8_t n_push_constant_bytes;

	/* RASTERIZATION PIPELINE STATES */

	uint8_t culled_winding;	// 0=no culling, 1=cw, 2=ccw, 3=cw+ccw
	uint8_t primitive_type; // 0=triangles, 1=lines, 2=points
	uint8_t depth_pass;	// condition for depth test pass
	uint8_t depth_enabled;	// whether or not writing depth is enabled
	uint8_t cw_stencil_ref;	// stencil test func reference for cw faces
	uint8_t cw_stencil_pass;// stencil test func pass condition for cw faces
	uint8_t cw_stencil_op_sfail;	// cw face stencil operation if stencil test fails
	uint8_t cw_stencil_op_spass_dfail;	// cw face stencil operation if stencil test passes but depth test fails
	uint8_t cw_stencil_op_sfail_dfail;	// cw face stencil operation if both stencil and depth test fail
	uint8_t cw_stencil_func_mask;	// stencil func mask for cw faces
	uint8_t cw_stencil_write_mask;	// stencil write mask for cw faces
	uint8_t ccw_stencil_ref;	// stencil test func reference for ccw faces
	uint8_t ccw_stencil_pass;// stencil test func pass condition for ccw faces
	uint8_t ccw_stencil_op_sfail;	// ccw face stencil operation if stencil test fails
	uint8_t ccw_stencil_op_spass_dfail;	// ccw face stencil operation if stencil test passes but depth test fails
	uint8_t ccw_stencil_op_sfail_dfail;	// ccw face stencil operation if both stencil and depth test fail
	uint8_t ccw_stencil_func_mask;	// stencil func mask for ccw faces
	uint8_t ccw_stencil_write_mask;	// stencil write mask for ccw faces
	uint8_t color_write_mask;	// color write mask (which color components can be written to; RGBA bits w/ A at LSB)
	uint8_t n_enabled_attachments;	// number of enabled color attachments (0-7, 0 corresponds to 1)
	uint8_t color_blend_op;	// the RGB blending operation
	uint8_t src_color_blend_fac;	// the source RGB blending factor
	uint8_t dst_color_blend_fac;	// the destination RGB blending factor
	uint8_t alpha_blend_op;	// the alpha blending operation
	uint8_t src_alpha_blend_fac;	// the source alpha blending factor
	uint8_t dst_alpha_blend_fac;	// the destination alpha blending factor
} pipeline_t;

// add null-terminated string str2 to the end of null-terminated string str1 
void str_add(char** str1, const char* str2) {
	uint32_t len1 = strlen(*str1), len2 = strlen(str2);
	*str1 = realloc(*str1, len1+len2+1);
	memcpy(*str1 + len1, str2, len2);
	(*str1)[len1+len2] = '\0';
}

// insert a null-terminated string str2 starting at index pos in null-terminated string str1
void str_insert(char** str1, const char* str2, uint32_t pos) {
	uint32_t len1 = strlen(*str1), len2 = strlen(str2);
	*str1 = realloc(*str1, len1+len2+1);
	memcpy(*str1 + pos + len2, *str1 + pos, len1-pos);
	memcpy(*str1 + pos, str2, len2);
	(*str1)[len1+len2] = '\0';
}

// add shader data type name to the end of a null-terminated string given type's number
void str_add_type(char** str, uint8_t type) {
	switch(type) {
		case 0: str_add(str, "vec2"); break;
		case 1: str_add(str, "vec3"); break;
		case 2: str_add(str, "vec4"); break;
		case 3: str_add(str, "ivec2"); break;
		case 4: str_add(str, "ivec3"); break;
		case 5: str_add(str, "ivec4"); break;
		case 6: str_add(str, "uvec2"); break;
		case 7: str_add(str, "uvec3"); break;
		case 8: str_add(str, "uvec4"); break;
		case 9: str_add(str, "mat2"); break;
		case 10: str_add(str, "mat2x3"); break;
		case 11: str_add(str, "mat2x4"); break;
		case 12: str_add(str, "mat3x2"); break;
		case 13: str_add(str, "mat3"); break;
		case 14: str_add(str, "mat3x4"); break;
		case 15: str_add(str, "mat4x2"); break;
		case 16: str_add(str, "mat4x3"); break;
		case 17: str_add(str, "mat4"); break;
		case 18: str_add(str, "float"); break;
		case 19: str_add(str, "int"); break;
		case 20: str_add(str, "uint"); break;
		case 21: str_add(str, "sampler2D"); break;
		case 22: str_add(str, "isampler"); break;
		case 23: str_add(str, "usampler"); break;
	}
}

// add an unsigned integer to the end of a null-terminated string
void str_add_ui(char** str, uint32_t x) {
	uint32_t str_size = 0;
	uint32_t n_digits = (x/10)+1;	// this is how much to expand the length of the string by
	for(uint32_t i = 0; i < 1000; i++) { str_size++; if((*str)[i] == '\0') break; }
	*str = realloc(*str, str_size+n_digits);
	sprintf(*str+str_size-1, "%u", x);
}

void str_add_i(char** str, int32_t x) {
	uint32_t str_size = 0;
	uint32_t n_digits = (abs(x)/10)+1;	// this is how much to expand the length of the string by
	if(x<0) n_digits++;
	for(uint32_t i = 0; i < 1000; i++) { str_size++; if((*str)[i] == '\0') break; }
	*str = realloc(*str, str_size+n_digits);
	sprintf(*str+str_size-1, "%i", x);
}

void str_add_f(char** str, float x) {
	char fstring[32];
	sprintf(fstring, "%f", x);
	str_add(str, fstring);
}

// shorthand to add the .x, .y, .z, or .w for a vector to the end of a null-terminated string
void str_add_vec_idx(char** str, uint32_t idx) {
	switch(idx) {
		case 0: str_add(str, ".x"); break;
		case 1: str_add(str, ".y"); break;
		case 2: str_add(str, ".z"); break;
		case 3: str_add(str, ".w"); break;
	}
}

// shorthand to add the [col][row] for a matrix to the end of a null-terminated string
void str_add_mat_idx(char** str, uint8_t width, uint8_t height, uint32_t idx) {
	str_add(str, "[");
	str_add_ui(str, idx/height);
	str_add(str, "][");
	str_add_ui(str, idx%width);
	str_add(str, "]");
}

// shorthand to add the loop counter that corresponds to the shader's scope_level to the end of a null-terminated string
void str_add_iterator(char** str, uint8_t scope_level) {
	if(!scope_level || scope_level > 8) return;
	char x[2]; x[1] = '\0';
	x[0] = "ijklmnop"[scope_level-1];
	str_add(str, (const char*)&x);
}

#define VAR_DEF_BIT 0x1
#define UNIF_DEF_BIT	0x2
#define IN_ATTR_DEF_BIT 0x4
#define OUT_ATTR_DEF_BIT 0x8
#define RAY_ATTR_DEF_BIT 0x10
#define INCOMING_RAY_ATTR_DEF_BIT 0x20
#define FUNC_DEF_BIT 0x40
#define ALL_DEF_BIT (VAR_DEF_BIT|UNIF_DEF_BIT|IN_ATTR_DEF_BIT|OUT_ATTR_DEF_BIT|RAY_ATTR_DEF_BIT|INCOMING_RAY_ATTR_DEF_BIT|FUNC_DEF_BIT)

typedef struct func_def_t func_def_t;

typedef struct shader_data_t {
	// USED FOR CHECKING SET/BINDING AND LOCATION EXISTENCES:
	uint8_t* sets;		// the set number(s) for each data block within the shader
	uint32_t* bindings;	// parallel to sets array; the binding numbers for each data block within the shader
	uint8_t* set_binding_types; // parallel to sets + bindings arrays; the types for each set/binding pair (0=uniform, 1=storage, 2=sampler, 3=image, 4=AS)
	uint16_t* locations;	// the location IDs occupied within the shader
	uint32_t n_set_binding_pairs;
	uint32_t n_locations;
	uint32_t n_push_constant_bytes;

	uint16_t* vertex_output_ids;	// the identifier for each vertex shader attribute output
	uint8_t* vertex_output_types;	// the data for each vertex shader attribute output
	uint8_t* vertex_output_modes;	// the interpolation mode for each vertex shader attribute output
	uint16_t* pixel_input_ids;		// the identifier for each pixel shader attribute input
	uint8_t* pixel_input_types;		// the data type for each pixel shader attribute input
	uint32_t n_vertex_outputs;
	uint32_t n_pixel_inputs;

	definition_t* defs;
	uint32_t n_defs;
} shader_data_t;

// add a set+binding pair to shader data (type; 0=uniform, 1=storage, 2=sampler, 3=image, 4=AS)
void add_set_binding(shader_data_t* data, uint8_t set, uint32_t binding, uint8_t type) {
	data->sets = realloc(data->sets, data->n_set_binding_pairs+1);
	data->bindings = realloc(data->bindings, 4*(data->n_set_binding_pairs+1));
	data->set_binding_types = realloc(data->set_binding_types, data->n_set_binding_pairs+1);
	data->sets[data->n_set_binding_pairs] = set;
	data->bindings[data->n_set_binding_pairs] = binding;
	data->set_binding_types[data->n_set_binding_pairs] = type;
	data->n_set_binding_pairs++;
}

// add a location to shader data
void add_location(shader_data_t* data, uint16_t location) {
	data->locations = realloc(data->locations, 2*(data->n_locations+1));
	data->locations[data->n_locations] = location;
	data->n_locations++;
}

void add_vertex_output(shader_data_t* data, uint16_t id, uint8_t type, uint8_t mode) {
	data->vertex_output_ids = realloc(data->vertex_output_ids, sizeof(uint16_t*)*(data->n_vertex_outputs+1));
	data->vertex_output_types = realloc(data->vertex_output_types, sizeof(uint8_t*)*(data->n_vertex_outputs+1));
	data->vertex_output_modes = realloc(data->vertex_output_modes, sizeof(uint8_t*)*(data->n_vertex_outputs+1));
	data->vertex_output_ids[data->n_vertex_outputs] = id;
	data->vertex_output_types[data->n_vertex_outputs] = type;
	data->vertex_output_modes[data->n_vertex_outputs] = mode;
	data->n_vertex_outputs++;
}

void add_pixel_input(shader_data_t* data, uint16_t id, uint8_t type) {
	data->pixel_input_ids = realloc(data->pixel_input_ids, sizeof(uint16_t*)*(data->n_pixel_inputs+1));
	data->pixel_input_types = realloc(data->pixel_input_types, sizeof(uint8_t*)*(data->n_pixel_inputs+1));
	data->pixel_input_ids[data->n_pixel_inputs] = id;
	data->pixel_input_types[data->n_pixel_inputs] = type;
	data->n_pixel_inputs++;
}

uint8_t check_set_binding_existence(shader_data_t* data, uint8_t set, uint16_t binding) { 
	for(uint32_t i = 0; i < data->n_set_binding_pairs; i++) if(data->sets[i]==set&&data->bindings[i]==binding) return 1; 
	return 0;
}
uint8_t check_location_existence(shader_data_t* data, uint16_t location) {
	for(uint32_t i = 0; i < data->n_locations; i++) if(data->locations[i]==location) return 1;
	return 0;
}

// created for each defined identifer; function, variable, uniform, in/out attrib, ray attribute, and incoming ray attrib identifier
struct definition_t {
	uint16_t id;
	uint8_t def_type;			// function, variable, uniform, in/out attrib, ray attrib; set to the *_DEF_BIT macro definitions
	uint8_t data_type;			// data type (0=vec2, 1=vec3, 2=vec4, 3=ivec2, 4=ivec3, 5=ivec4, 6=uvec2, 7=uvec3, 8=uvec4, 9=mat2x2, 10=mat2x3, 11=mat2x4,
		// 12=mat3x2, 13=mat3x3, 14=mat3x4, 15=mat4x2, 16=mat4x3, 17=mat4x4, 18=float, 19=signed integer, 20=unsigned integer,
		// 21=sampler, 22=isampler, 23=usampler, 24=image, 25=acceleration structure) - N/A if function definition
	uint16_t elcount;		// how many data elements defined at this identifier, 0 corresponds to 1 - N/A if function definition
	uint16_t location_id;	// for payload/incoming payload blocks, attribute definitions; 0 for uniforms, 1 for push constant uniforms
	uint8_t within_block;	// whether or not this was defined within a uniform/storage block
	uint8_t set;			// uniform block set number
	uint32_t binding;		// uniform block binding number
	func_def_t* func_def;	// pointer to function defined under this identifier
};

// created for each defined function; information about parameters
struct func_def_t {
	uint32_t n_params;		// how many parameters there are
	uint16_t* param_ids;	// identifiers for each parameter (defined locally as variables at beginning of the function body scope)
	uint16_t* param_elcounts; // how many data elements defined for this parameter, 0 corresponds to 1
	uint8_t* param_types;	// data type of each parameter (0=vec2, 1=vec3, 2=vec4, 3=ivec2, 4=ivec3, 5=ivec4, 6=uvec2, 7=uvec3, 8=uvec4, 9=mat2x2, 10=mat2x3, 11=mat2x4,
		// 12=mat3x2, 13=mat3x3, 14=mat3x4, 15=mat4x2, 16=mat4x3, 17=mat4x4, 18=float, 19=signed integer, 20=unsigned integer,
		// 21=sampler, 22=isampler, 23=usampler, 24=image, 25=acceleration structure)
};

// check if a specified identifier exists
// filter is OR'd together bits; everthing to search for
// returns 0 if the identifier does not exist, address to the existing definition_t otherwise
definition_t* check_identifier_existence(uint16_t id, definition_t* defs, uint32_t n_defs, uint32_t filter) {
	if(defs)
		for(uint32_t i = 0; i < n_defs; i++) if(defs[i].def_type & filter && defs[i].id == id) return &defs[i];
	return 0;
}

// shorthand for check_identifier_existence; check all definition types but allows to filter out (exclude) some 
definition_t* check_identifier_existence_excl(uint16_t id, definition_t* defs, uint32_t n_defs, uint32_t excl_filter) {
	return check_identifier_existence(id,defs,n_defs,ALL_DEF_BIT & (~excl_filter));
}

// defines a new identifier
// pushes its data structure to the back of defs array
void add_definition(definition_t** defs, uint32_t* n_defs, uint16_t id, uint8_t def_type, uint8_t data_type, uint16_t elcount,
	uint16_t location_id, uint8_t within_block, uint8_t set, uint32_t binding, func_def_t* func_def) {
	*defs = realloc(*defs, sizeof(definition_t)*((*n_defs)+1));
	(*defs)[*n_defs].id = id;
	(*defs)[*n_defs].def_type = def_type;
	(*defs)[*n_defs].data_type = data_type;
	(*defs)[*n_defs].elcount = elcount;
	(*defs)[*n_defs].location_id = location_id;
	(*defs)[*n_defs].within_block = within_block;
	(*defs)[*n_defs].set = set;
	(*defs)[*n_defs].binding = binding;
	(*defs)[*n_defs].func_def = func_def;
	(*n_defs)++;
}

// returns 1 if the definition type specified by def_type can be an array, and 0 otherwise
uint8_t check_def_type_array(uint32_t def_type) {
	return (def_type&(VAR_DEF_BIT|UNIF_DEF_BIT|RAY_ATTR_DEF_BIT|INCOMING_RAY_ATTR_DEF_BIT)) > 0;	// variables, uniforms, and ray attributes can be arrays
}

// given a type, add typecast to higher precision as required (uint/int/float required is stated in types)
// bitwise OR together the results from check_type for 'types'
void str_add_typecast(char** str, uint8_t full_vector, uint8_t n_vector_elements, uint8_t types) {
	if(!full_vector) {	// if not a full vector, then dealing with a scalar
		if(types == 0) str_add(str, "uint(");
		if(types == 1) str_add(str, "int(");
		if(types > 1)  str_add(str, "float(");
	}
	else {
		switch(n_vector_elements) {
			case 2:
				if(types == 0) str_add(str, "uvec2(");
				if(types == 1) str_add(str, "ivec2(");
				if(types > 1)  str_add(str, "vec2(");
				break;
			case 3:
				if(types == 0) str_add(str, "uvec3(");
				if(types == 1) str_add(str, "ivec3(");
				if(types > 1)  str_add(str, "vec3(");
				break;
			case 4:
				if(types == 0) str_add(str, "uvec4(");
				if(types == 1) str_add(str, "ivec4(");
				if(types > 1)  str_add(str, "vec4(");
				break;
		}
	}
}

// returns 0 if a shader data type is unsigned, 1 if signed integer, and 2 if floating-point
uint8_t base_type(uint8_t type) {
	if((type >= 6 && type <= 8) || type == 20 || type == 23) return 0;
	else if((type >= 3 && type <= 5) || type == 19 || type == 22) return 1;
	else return 2;
}

void str_add_constant(char** str, uint32_t constant, uint8_t type) {
	if(type == 0) {
		str_add(str, "uint(");
		str_add_ui(str, constant);
		str_add(str, ")");
	}
	if(type == 1) str_add_i(str, *(int32_t*)&constant);
	if(type  > 1) str_add_f(str, *(float*)&constant);
}

void str_add_operation(char** str, uint8_t operation) {
	switch(operation) {
		case 0x0: str_add(str, " + "); break;
		case 0x1: str_add(str, " * "); break;
		case 0x2: str_add(str, " / "); break;
		case 0x3: str_add(str, " - "); break;
		case 0x4: str_add(str, ", "); break;
	}
}

#define IDX_TYPE_LOOP -1
#define IDX_TYPE_VAR -2
#define IDX_TYPE_UNIFORM -3
#define IDX_TYPE_INSTANCE -4

// will load an array index from a shader and return the index; adds to the pointer to the array index
// returns a value >= 0 if constant array index, -5 if end of shader is encountered (t), and an IDX_TYPE_* value otherwise
int32_t read_array_idx(uint16_t* array_index, uint16_t* identifier, uint16_t* multiplier, int32_t* offset, uint8_t* max_addr) {
	if(*array_index == 65533) {	// use instance ID, with multiplier, offset
		if((uint8_t*)array_index + 7 > max_addr) return -5;
		array_index++;
		*multiplier = *array_index + 1;
		array_index++;
		*offset = *(int32_t*)array_index;
		return IDX_TYPE_INSTANCE;
	}
	if(*array_index == 65534) {	// use uint uniform, with multiplier, offset
		if((uint8_t*)array_index + 9 > max_addr) return -5;
		array_index++;
		*identifier = *array_index;
		array_index++;
		*multiplier = *array_index + 1;
		array_index++;
		*offset = *(int32_t*)array_index;
		return IDX_TYPE_UNIFORM;
	}
	if(*array_index == 65535) {	// 65535 65535 - current loop iteration
		if((uint8_t*)array_index + 3 > max_addr) return -5;
		array_index++;	// after the first; only if the index is 32-bit
		if(*array_index == 65535) return IDX_TYPE_LOOP;	// current loop iteration as index
		*identifier = *array_index;
		return IDX_TYPE_VAR;	// uint variable as index
	}
	return *array_index;
}

// reads the common form of {identifier | index} without the 8 bits for vector/matrix element; puts its data into newly defined variables, returns if invalid
// opcode is assumed to be at the identifier, and will be shifted forward to the address after the index if fail
#define READ_ID(n,exclude_filter) definition_t *def_ptr##n; \
	uint16_t id##n = READ(opcode,2); \
	if(!(def_ptr##n=check_identifier_existence_excl(id##n,defs,n_defs,exclude_filter))) return 1; /* identifier does not exist */ \
	uint8_t is_arr##n = 0, type##n = def_ptr##n->data_type, elcount##n = def_ptr##n->elcount; \
	is_arr##n = check_def_type_array(def_ptr##n->def_type); \
	uint16_t idx_id##n, multiplier##n; \
	int32_t offset##n, arr_idx##n = is_arr##n ? read_array_idx((uint16_t*)(opcode+2), &idx_id##n, &multiplier##n, &offset##n, end) : 0; \
	if(is_arr##n && arr_idx##n < IDX_TYPE_INSTANCE) return 1; \
	if(arr_idx##n >= 0 && arr_idx##n > elcount##n) return 1; /* using constant as index and the index does not exist */ \
	if(arr_idx##n == IDX_TYPE_LOOP && (level_status[scope_level-1]!=2||level_iterations[scope_level-1]-1>elcount##n)) return 1; /* using current loop iteration and this level is not in loop/has too many iterations*/ \
	if(arr_idx##n == IDX_TYPE_VAR && (shader_type != 0 || def_ptr##n->def_type != UNIF_DEF_BIT)) return 1; /* using uint variable as index and not vertex shader with uniform arrays */ \
	if(arr_idx##n == IDX_TYPE_VAR && !check_identifier_existence(idx_id##n,defs,n_defs,VAR_DEF_BIT)) return 1; \
	if(arr_idx##n == IDX_TYPE_VAR && check_identifier_existence(idx_id##n,defs,n_defs,VAR_DEF_BIT)->elcount != 1) return 1; \
	if(arr_idx##n == IDX_TYPE_UNIFORM && !check_identifier_existence(idx_id##n,defs,n_defs,UNIF_DEF_BIT)) return 1; \
	if(arr_idx##n == IDX_TYPE_UNIFORM && check_identifier_existence(idx_id##n,defs,n_defs,UNIF_DEF_BIT)->elcount != 1) return 1; \
	if(arr_idx##n == IDX_TYPE_INSTANCE && shader_type != 0) return 1; /* instance ID can only be used as index in vertex shaders */ \
	opcode += 2; \
	if(is_arr##n && arr_idx##n >= 0) opcode += 2;	/* array index provided by constant index */ \
	else if(is_arr##n && arr_idx##n == IDX_TYPE_LOOP) opcode += 4;	/* array index provided by current loop iteration (index is 2 16-bit uints equal to 65535) */ \
	else if(is_arr##n && arr_idx##n == IDX_TYPE_VAR) opcode += 4;	/* array index provided by uint variable */ \
	else if(is_arr##n && arr_idx##n == IDX_TYPE_UNIFORM) opcode += 10;	/* array index provided by uint uniform */ \
	else if(is_arr##n && arr_idx##n == IDX_TYPE_INSTANCE) opcode += 8;	/* array index provided by instance ID */

// reads the common form of {identifier | index | 8 bits for vec/mat element}; puts its data into newly defined variables, returns if invalid
// opcode is assumed to be at the beginning of the id
#define READ_ID_WITH_MATVEC_ELEMENT(n,exclude_filter) READ_ID(n,exclude_filter);\
	uint8_t matvec_idx##n = type##n<18 ? READ(opcode,1) : 0; if(type##n<18) opcode++; /* matrix/vector element will follow array index if type < 18 (is matrix or vector) */

// used to add index [] to shader; n should be the same as in READ_ID
#define ADD_IDX(n) { \
	if(is_arr##n) add_idx(&glsl_shader->src, idx_id##n, arr_idx##n, multiplier##n, offset##n, scope_level); \
}

// implementation for ADD_IDX(n) macro
void add_idx(char** str, uint16_t idx_id, int32_t arr_idx, uint16_t multiplier, int32_t offset, uint8_t scope_level) {
	str_add(str, "["); \
	if(arr_idx >= 0) str_add_ui(str, arr_idx);
	else if(arr_idx == IDX_TYPE_LOOP) str_add_iterator(str, scope_level-1);
	else if(arr_idx == IDX_TYPE_VAR) {
		str_add(str, "int(_");
		str_add_ui(str, idx_id);
		str_add(str, "[0])");
	} else if(arr_idx == IDX_TYPE_UNIFORM) {
		str_add_ui(str, multiplier);
		str_add(str, "*int(_");
		str_add_ui(str, idx_id);
		str_add(str, "[0])");
		if(offset >= 0) str_add(str, "+");
		str_add_i(str, offset);
	} else if(arr_idx == IDX_TYPE_INSTANCE) {
		str_add_ui(str, multiplier);
		str_add(str, "*gl_InstanceID");
		if(offset >= 0) str_add(str, "+");
		str_add_i(str, offset);
	}
	str_add(str, "]");
}

// returns 1 on fail, returns 0 and fills glsl_shader on success
uint8_t build_shader(uint8_t* src, uint32_t length, uint8_t shader_type, shader_t* glsl_shader, shader_data_t* shader_data) {
	if(length == 0) return 1;
	// shader_type: 0=vertex, 1=pixel, 2=compute, 3=other (RT shader; unsupported + bytecode exclusive to RT shaders will be treated as invalid)

	uint64_t loadval(uint8_t* a, uint8_t n);
	uint8_t err = 0;	// checked at the beginning of instruction processing loop
	#define READ(ptr,n_bytes) (ptr+n_bytes-1 > end ? (err=1) : loadval(ptr,n_bytes))	/* shorthand to read a value from the shader safely */
	// check if identifier exists, return 1 if it does
	#define CHECK_ID_DEFINED(id) if(check_identifier_existence(id,defs,n_defs,ALL_DEF_BIT)) return 1
	#define MAT_WIDTH(type) (((type-9)/3)+2)		/* gets the matrix width given a matrix type # */
	#define MAT_HEIGHT(type) (((type-9)%3)+2)	/* gets the matrix height given a matrix type # */
	#define MAT_SIZE(type) (MAT_WIDTH(type)*MAT_HEIGHT(type))	/* gets the number of matrix elements given a matrix type */
	#define VEC_SIZE(type) ((type%3)+2)	/* gets the number of vector elements given a vector type */

	// previously globally defined identifiers, their element counts, and their types
	// there are functions, uniforms, variables, in/out attributes, ray attributes, and incoming ray attributes
	definition_t* defs = 0;	// array of all defined identifiers + their information
	uint32_t n_defs = 0;			// total number of identifier definitions
	uint32_t n_local_defs = 0;	// number of variables defined within a function; remove this number from back of definitions array when the function is exited

	// scope information
	uint8_t scope_type = 0;			// current scope type (0=global, 1=main func, 2=func, 3=uniform block, 4=push constant block, 5=storage block, 6=ray payload block, 
		// 7=incoming ray payload block, 8=func definition)
	uint8_t scope_level = 0;	// 0=not in function, 1=level 1, 2=level 2, ..., 9=level 8

	uint8_t has_push_constants = 0; // whether or not a push constants block was defined in the shader

	// at if/elif/else opcode, scope_level++ and then the level is set to be in conditional; if extensions is enabled for level by if opcodes + disabled by else opcodes
	// 		a level's allow_if_extension is disabled at the closer if a conditional opcode doesn't directly follow the closer byte
	// at loop opcode, scope_level++ and then the level's iteration count is set
	// at loop/conditional closer, scope_level--
	uint8_t level_status[8];	// 1 if level is in conditional, 2 if level is in loop, 0 if neither of these are true (always 0 for level 1)
	uint16_t level_iterations[8];	// the loop iteration count for each level (0 if level is not in loop)
	uint8_t level_allow_if_extension[8];	// whether or not the conditional on a level can be extended by an elif/else 
		// (set at an if, unset at an else, and unset at a closer if an elif/else opcode doesn't follow the closer byte)
	for(uint8_t i = 0; i < 8; i++) level_status[i] = level_iterations[i] = level_allow_if_extension[i] = 0;

	uint32_t n_result_scalars = 0; // this is used for vector = scalar op scalar operations; on line prior a result scalar is defined to then assign all vector elements

	uint32_t entry_point = 0, modified_frag_depth = 0;	// used for inserting gl_FragDepth = gl_FragCoord.z; when needed

	str_add(&glsl_shader->src, "#version 330 core\n");
	uint8_t* opcode = src;
	uint8_t* end = src+length-1;
	while(opcode <= end) {
		if(err) return 1;	// there was an error loading a value for last instruction processed (a value read previously exceeded end of the shader)
		uint8_t op_byte = *opcode;
		if(*opcode >= 0x00 && *opcode <= 0x03) {	// in/out attribute definition
			if(shader_type == 2 || shader_type == 3) return 1;	// attributes can't be defined in compute or RT shaders
			if(scope_type != 0) return 1;	// can only define attrib in global scope
			uint16_t id = READ(opcode+2,2);
			uint16_t location_id = 0; // only present for vertex input/pixel output
			if((shader_type == 0 && op_byte == 0) || (shader_type == 1 && op_byte != 0)) location_id = READ(opcode+4,2);
			uint8_t type = READ(opcode+1,1);
			if(type >= 21) return 1; // can't define sampler attrib (21, 22, 23), image attrib (24), or AS attrib (25); > 25 is invalid
			CHECK_ID_DEFINED(id);	// make sure the identifier isn't already defined
			if((shader_type == 0 && op_byte == 0) || (shader_type == 1 && op_byte != 0)) {	// vertex inputs + pixel outputs have location IDs
				if(check_location_existence(shader_data,location_id)) return 1;
				add_location(shader_data,location_id);
			}
			if(op_byte == 0x00) add_definition(&defs, &n_defs, id, IN_ATTR_DEF_BIT, type, 0, location_id, 0, 0, 0, 0);
			else add_definition(&defs, &n_defs, id, OUT_ATTR_DEF_BIT, type, 0, location_id, 0, 0, 0, 0);
			if((shader_type == 0 && op_byte == 0) || (shader_type == 1 && op_byte != 0)) {	// vertex inputs + pixel outputs have location IDs
				str_add(&glsl_shader->src, "layout(location = ");
				str_add_ui(&glsl_shader->src, location_id);
				str_add(&glsl_shader->src, ") ");
			}
			if(shader_type == 1 && op_byte == 0x00) { // pixel shader input definition
				uint8_t found_matching_output = 0;
				for(uint32_t i = 0; i < shader_data->n_vertex_outputs; i++) {
					if(shader_data->vertex_output_ids[i] == id) {
						switch(shader_data->vertex_output_modes[i]) {
							case 1: str_add(&glsl_shader->src, "flat "); break;
							case 2: str_add(&glsl_shader->src, "smooth "); break;
							case 3: str_add(&glsl_shader->src, "noperspective "); break;
						}
						found_matching_output = 1;
						break;
					}
				}
				if(!found_matching_output) return 1;
				add_pixel_input(shader_data, id, type);
			}
			if(op_byte == 0x00) str_add(&glsl_shader->src, "in ");
			else if(shader_type == 0) {
				if(op_byte != 0x01 && base_type(type) != 2) return 1; // int vertex output must be defined as flat
				switch(op_byte) {
					case 0x01: str_add(&glsl_shader->src, "flat out "); break;
					case 0x02: str_add(&glsl_shader->src, "smooth out "); break;
					case 0x03: str_add(&glsl_shader->src, "noperspective out "); break;
				}
				add_vertex_output(shader_data, id, type, op_byte);	// add vertex shader attribute output to shader_data ('mode' is = op_byte)
			} else if(op_byte > 0x00) str_add(&glsl_shader->src, "out ");
			str_add_type(&glsl_shader->src, type);
			str_add(&glsl_shader->src, " _");
			str_add_ui(&glsl_shader->src, id);
			str_add(&glsl_shader->src, ";\n");
			if((shader_type == 0 && op_byte == 0) || (shader_type == 1 && op_byte != 0)) opcode += 6; // move to next instruction
			else opcode += 4;	// move to next instruction (no location ID)
		} else if(*opcode == 0x04) {	// uniform definition
			if(scope_type != 3 && scope_type != 4) return 1;	// can't define uniform unless in uniform or push constant block
			uint16_t id = READ(opcode+2,2);
			uint16_t elcount = READ(opcode+4,2);
			uint8_t type = READ(opcode+1,1);
			if(type >= 21 && type <= 25) {	// sampler, image, or AS uniform
				if(scope_type != 3) return 1;	// these can only be defined within uniform blocks
				if(READ(opcode-6,1) != 0x07 || READ(opcode+6,1) != 0x08) return 1; // uniform block opener should precede and uniform block closer should follow
				if(type < 24 && elcount == 0 && (scope_type != 3 || READ(opcode+6,1) != 0x08 || shader_type != 3)) return 1;	// can only define unsized sampler array as last uniform in storage block, in RT shaders
				if(type == 24 && shader_type < 2) return 1;		// image uniforms can only be defined in compute or RT shaders 
				if(type == 25 && shader_type != 3) return 1;	// AS uniforms can only be defined in RT shaders
				if(type > 23 && elcount == 0) return 1; // only samplers are ever allowed to be unsized uniform arrays
			}
			else if(type > 25) return 1;
			else if(elcount == 0) return 1;
			CHECK_ID_DEFINED(id);	// make sure the identifier isn't already defined
			if(scope_type == 3) add_definition(&defs, &n_defs, id, UNIF_DEF_BIT, type, elcount, 0, 1, shader_data->sets[shader_data->n_set_binding_pairs-1], shader_data->bindings[shader_data->n_set_binding_pairs-1], 0);
			else {
				shader_data->n_push_constant_bytes += 4*(type < 9 ? VEC_SIZE(type) : 1)*(type >= 9 && type < 18 ? MAT_SIZE(type) : 1);
				add_definition(&defs, &n_defs, id, UNIF_DEF_BIT, type, elcount, 1, 1, 0, 0, 0);
			}
			str_add(&glsl_shader->src, "uniform "); // the VM currently does not support uniform blocks; otherwise this would not be required except for w/ samplers/images
			str_add_type(&glsl_shader->src, type);
			str_add(&glsl_shader->src, " _");
			str_add_ui(&glsl_shader->src, id);
			str_add(&glsl_shader->src, "[");
			str_add_ui(&glsl_shader->src, elcount);
			str_add(&glsl_shader->src, "]");
			str_add(&glsl_shader->src, ";\n");
			opcode += 6;	// move to next instruction
		} else if(*opcode == 0x05) {	// variable definition
			if((scope_type > 2 && scope_type != 5) || scope_level > 1) return 1; // variables can only be defined globally, at level 0/1, or in a storage block
			uint16_t id = READ(opcode+2,2);
			uint16_t elcount = READ(opcode+4,2);
			uint8_t type = READ(opcode+1,1);
			if(elcount == 0 && !(scope_type == 5 && READ(opcode+6,1) != 0x08)) return 1; // can only define unsized variable as last variable in a storage block
			if(type > 20) return 1;	// can't define sampler variable (21, 22, 23), image variable (24), or AS variable (25); > 25 is invalid
			CHECK_ID_DEFINED(id);	// make sure the identifier isn't already defined
			add_definition(&defs, &n_defs, id, VAR_DEF_BIT, type, elcount, 0, 0, 0, 0, 0);
			if(scope_level == 1) n_local_defs++;	// if at level 1 of a function, this variable definition is a local variable
			str_add_type(&glsl_shader->src, type);
			str_add(&glsl_shader->src, " _");
			str_add_ui(&glsl_shader->src, id);
			str_add(&glsl_shader->src, "[");
			str_add_ui(&glsl_shader->src, elcount);
			str_add(&glsl_shader->src, "];\n");
			opcode += 6;
		} else if(*opcode == 0x06) {	// ray attribute definition
			if(shader_type != 3) return 1;	// ray attributes can only be defined in RT shaders
			if(scope_type != 6 && scope_type != 7) return -1;	// ray attribs can only be defined in ray paylod or incoming ray payload blocks
			uint16_t id = READ(opcode+2,2);
			uint16_t elcount = READ(opcode+4,2);
			uint8_t type = READ(opcode+1,1);
			if(elcount == 0) return 1;
			if(type > 20) return 1;	// can't define sampler ray attrib (21, 22, 23), image ray attrib (24), or AS ray attrib (25); > 25 is invalid
			CHECK_ID_DEFINED(id);	// make sure the identifier isn't already defined
			if(scope_type == 6) add_definition(&defs, &n_defs, id, RAY_ATTR_DEF_BIT, type, elcount, shader_data->locations[shader_data->n_locations-1], 0, 0, 0, 0);
			if(scope_type == 7)	add_definition(&defs, &n_defs, id, INCOMING_RAY_ATTR_DEF_BIT, type, elcount, shader_data->locations[shader_data->n_locations-1], 0, 0, 0, 0);
			opcode += 6;
		} else if(*opcode == 0x07) {	// uniform block opener
			if(scope_type != 0) return 1;	// can only open uniform block in global scope
			if(READ(opcode+6,1) != 0x04) return 1;	// uniform block needs to have at least one uniform definition
			uint8_t set = READ(opcode+1,1);
			uint32_t binding = READ(opcode+2,4);
			if(check_set_binding_existence(shader_data,set,binding)) return 1;
			if(READ(opcode+7,1) >= 21 && READ(opcode+7,1) <= 23) // uniform def. after opener is a sampler
				add_set_binding(shader_data,set,binding,2);
			else if(READ(opcode+7,1) == 24) add_set_binding(shader_data,set,binding,3); // uniform def. after opener is an image
			else if(READ(opcode+7,1) == 25) add_set_binding(shader_data,set,binding,4); // uniform def. after opener is an AS
			else add_set_binding(shader_data,set,binding,0);
			scope_type = 3;
			// the VM currently does not support uniform buffers; so they are not added to the GLSL
			/*str_add(&glsl_shader->src, "layout(std140) uniform _");
			str_add_ui(&glsl_shader->src, set);
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, binding);
			str_add(&glsl_shader->src, " {\n");*/
			opcode += 6;
		} else if(*opcode == 0x08) {	// block/function/conditional/loop closer
			if(scope_type == 1 && scope_level < 2) return 1;	// in main function + not in loop/conditional
			if(scope_type == 2 && scope_level < 2) {	// in function + not in loop/conditional; closing a function
				scope_type = 0;
				// remove n_local_defs definitions from the back of definitions array
				if(n_defs-n_local_defs > 0) { defs = realloc(defs, (n_defs-n_local_defs)*sizeof(definition_t)); }
				else {
					if(n_defs) free(defs);
					defs = 0; n_defs = 0;
				}
				n_local_defs = 0;
			}
			if((scope_type == 1 || scope_type == 2) && scope_level >= 2) {	// if in function + present in loop/conditional
				if(level_status[scope_level-1] == 1) // this level is in conditional, unset allow_if_extension if elif or else opcode does not follow
					if(opcode+1 <= end && READ(opcode+1,1) != 0x60 && READ(opcode+1,1) != 0x61) level_allow_if_extension[scope_level-1] = 0;
				level_status[scope_level-1] = 0;	// this level is no longer in a loop/conditional
				level_iterations[scope_level-1] = 0;
				scope_level--;	// go back down a level
			}
			if(scope_type != 3 && scope_type != 4) { // the VM currently unravels uniform/push blocks; so no need to add } to the GLSL if closing a uniform/push block
				str_add(&glsl_shader->src, "}");
				if(scope_type >= 3 && scope_type <= 6) str_add(&glsl_shader->src, ";\n");	// close block
				else str_add(&glsl_shader->src, "\n");
			}
			if(scope_type >= 3 && scope_type <= 7) { scope_type = 0; scope_level = 0; }
			opcode++;
		} else if(*opcode == 0x09) {	// push constant block opener
			if(scope_type != 0) return 1;	// can only open push constant block in global scope
			if(has_push_constants) return 1;	// can only have one push constant block per shader
			if(READ(opcode+1,1) != 0x04) return 1;	// push constant block needs to have at least one uniform definition
			has_push_constants = 1;
			scope_type = 4;
			opcode++;
		} else if(*opcode == 0x0A) {	// storage block opener
			if(scope_type != 0) return 1;	// can only open storage block in global scope
			if(shader_type != 2) return 1;	// storage blocks can only be present in compute shaders
			if(READ(opcode+6,1) != 0x05) return 1;	// storage block needs to have at least one variable definition
			uint8_t set = READ(opcode+1,1);
			uint32_t binding = READ(opcode+2,4);
			if(check_set_binding_existence(shader_data,set,binding)) return 1;
			add_set_binding(shader_data,set,binding,1);
			scope_type = 5;
			opcode += 6;
		} else if(*opcode == 0x0B || *opcode == 0x0C) {	// ray payload or incoming ray payload opener
			if(scope_type != 0) return 1;	// can only open ray payload block in global scope
			if(shader_type != 3) return 1;	// ray payload blocks can only be present in RT shaders
			if(READ(opcode+2,1) != 0x06) return 1;	// ray payload block needs to have at least one ray attribute defintion
			uint16_t payload_location = READ(opcode+1,1);
			if(check_location_existence(shader_data, payload_location)) return 1;
			add_location(shader_data,payload_location);	
			if(*opcode == 0x0B) scope_type = 6;
			else scope_type = 7;
			opcode += 3;
		} else if(*opcode == 0x0D) {	// function definition
			if(scope_type != 0) return 1;	// function definitions must occur in global scope
			// check that parameter definitions and then a function body opener follow
			uint8_t* p = opcode + 3;	// pointer to first parameter in list or function opener
			uint16_t id = READ(opcode+1,2);
			CHECK_ID_DEFINED(id);
			add_definition(&defs, &n_defs, id, FUNC_DEF_BIT, 0, 0, 0, 0, 0, 0, calloc(1,sizeof(func_def_t)));
			func_def_t* func_def = check_identifier_existence(id,defs,n_defs,FUNC_DEF_BIT)->func_def;
			func_def->param_ids = 0;
			func_def->param_elcounts = 0;
			func_def->param_types = 0;
			func_def->n_params = 0;
			str_add(&glsl_shader->src, "void _");
			str_add_ui(&glsl_shader->src, id);
			str_add(&glsl_shader->src, "(");
			// store all information on parameters; parameter definitions shouldn't do anything except ensure they're a part of a function definition
			while(1) {
				if(READ(p,1) < 0x0E || READ(p,1) > 0x11) return 1;	// neither a parameter or function opener
				if(READ(p,1) == 0xE) break;	// a function opener
				else {	// a parameter definition
					if(READ(p+1,1) > 20) return 1;	// invalid parameter type
					uint16_t param_id = READ(p+2,2);
					uint16_t param_elcount = READ(p+4,2);
					uint8_t param_type = READ(p+1,1);
					CHECK_ID_DEFINED(param_id);
					if(param_elcount == 0) return 1;
					if(param_type > 20) return 1;
					func_def->param_ids = realloc(func_def->param_ids, (func_def->n_params+1)*sizeof(uint16_t));
					func_def->param_elcounts = realloc(func_def->param_elcounts, func_def->n_params+1);
					func_def->param_types = realloc(func_def->param_types, func_def->n_params+1);
					func_def->param_ids[func_def->n_params] = param_id;
					func_def->param_elcounts[func_def->n_params] = param_elcount;
					func_def->param_types[func_def->n_params] = param_type;
					if(func_def->n_params > 0) str_add(&glsl_shader->src, ", ");
					func_def->n_params++;
					switch(READ(p,1)) {
						case 0x0F: str_add(&glsl_shader->src, "in "); break;
						case 0x10: str_add(&glsl_shader->src, "out "); break;
						case 0x11: str_add(&glsl_shader->src, "inout "); break;
					}
					str_add_type(&glsl_shader->src, param_type);
					str_add(&glsl_shader->src, " _");
					str_add_ui(&glsl_shader->src, param_id);
					str_add(&glsl_shader->src, "[");
					str_add_ui(&glsl_shader->src, param_elcount);
					str_add(&glsl_shader->src, "]");
					p += 6;
				}
			}
			str_add(&glsl_shader->src, ") ");
			scope_type = 8;
			opcode += p-opcode;
		} else if(*opcode == 0x0E) {	// function/conditional/loop opener
			if(scope_type == 8) {	// function opener if part of function definition
				scope_type = 2;
				scope_level = 1;
				opcode++;
			}
			else if(level_status[scope_level-1])	// loop or conditional opener if this level's conditional status is non-zero
				opcode++;
			else return 1;
			str_add(&glsl_shader->src, "{\n");
		} else if(*opcode >= 0x0F && *opcode <= 0x11) {	// parameter definition
			if(scope_type != 8) return 1;	// not part of func definition
			opcode += 6;
		} else if(*opcode == 0x12) {	// function call
			if(scope_type != 1)	return 1;	// a function call can only be present in the main function
			uint16_t func_id = READ(opcode+1,2);
			definition_t* def_ptr;
			if((def_ptr = check_identifier_existence(func_id,defs,n_defs,FUNC_DEF_BIT)) == 0) return 1;	// function does not exist
			func_def_t* f = def_ptr->func_def;
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, func_id);
			str_add(&glsl_shader->src, "(");
			// ensure that all arguments (variables) required are present and compatible with the parameters
			uint32_t p = 0;
			def_ptr = 0;
			for(p = 0; p < f->n_params; p++) {
				if(opcode+3+p*2 > end) return 1;
				def_ptr = check_identifier_existence(READ(opcode+3+p*2,2),defs,n_defs,VAR_DEF_BIT);
				if(!def_ptr) return 1;
				else if(f->param_elcounts[p] != def_ptr->elcount || f->param_types[p] != def_ptr->data_type) return 1;
				if(p > 0) str_add(&glsl_shader->src, ", ");
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, def_ptr->id);
			}
			str_add(&glsl_shader->src, ");\n");
			opcode += 3+p*2;
		} else if(*opcode == 0x13) {	// return from function
			if(scope_type != 1 && scope_type != 2) return 1;	// must only be present within a function
			str_add(&glsl_shader->src, "return;\n");
			opcode++;
		} else if(*opcode == 0x73) {
			if(scope_type != 1 && scope_type != 2) return 1;        // must only be present within a function
			if(shader_type != 1) return 1;  // discard can only be present within pixel shaders
			str_add(&glsl_shader->src, "discard;\n");
			opcode++;
		} else if(*opcode == 0x14) {	// open the main function
			if(scope_type != 0) return 1;	// must only be present within global scope
			scope_type = 1;
			scope_level = 1;
			str_add(&glsl_shader->src, "void main() {\n");
			entry_point = strlen(glsl_shader->src);
			opcode++;
		} else if(*opcode >= 0x15 && *opcode <= 0x27) {	// vector operations
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID(,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			if(type >= 9) return 1;	// not one of the vector types
			if(op_byte == 0x15 && type >= 6) return 1;	// can't use with uvec*
			if(op_byte == 0x16 && type >= 6) return 1;	// can't use with uvec*
			if(op_byte >= 0x17 && type >= 3) return 1;	// can't use with uvec*, ivec*
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			str_add(&glsl_shader->src, " = ");
			switch(op_byte) {
				case 0x15: str_add(&glsl_shader->src, "-");			 break; case 0x16: str_add(&glsl_shader->src, "abs(");	break;
				case 0x17: str_add(&glsl_shader->src, "normalize("); break; case 0x18: str_add(&glsl_shader->src, "floor(");break;
				case 0x19: str_add(&glsl_shader->src, "ceil(");		 break;	case 0x1A: str_add(&glsl_shader->src, "tan(");	break;
				case 0x1B: str_add(&glsl_shader->src, "sin(");	 	 break;	case 0x1C: str_add(&glsl_shader->src, "cos(");	break;
				case 0x1D: str_add(&glsl_shader->src, "atan(");  	 break;	case 0x1E: str_add(&glsl_shader->src, "asin("); break;
				case 0x1F: str_add(&glsl_shader->src, "acos(");  	 break;	case 0x20: str_add(&glsl_shader->src, "tanh("); break;
				case 0x21: str_add(&glsl_shader->src, "sinh(");  	 break;	case 0x22: str_add(&glsl_shader->src, "cosh("); break;
				case 0x23: str_add(&glsl_shader->src, "atanh("); 	 break;	case 0x24: str_add(&glsl_shader->src, "asinh(");break;
				case 0x25: str_add(&glsl_shader->src, "acosh("); 	 break;	case 0x26: str_add(&glsl_shader->src, "log(");	break;
				case 0x27: str_add(&glsl_shader->src, "log2(");  	 break;
			}
			str_add(&glsl_shader->src, "_"); 
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			if(op_byte > 0x15) str_add(&glsl_shader->src, ")");
			str_add(&glsl_shader->src, ";\n");
		} else if(*opcode >= 0x28 && *opcode <= 0x2A) {	// matrix operations
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID(1,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			READ_ID(2,FUNC_DEF_BIT);
			if(type1 < 9 || type1 > 18 || type2 < 9 || type2 > 17) return 1; // make sure the types of input+ouput are not invalid
			switch(op_byte) {
				case 0x28: if(type1 == 18 || type1 != type2) return 1; break; // inverse; input+output must be matrices of the same dimensions
				case 0x29: if(type1 != 18) return 1; break; // determinant; output must be float
				case 0x2A: if(type1 == 18 || MAT_WIDTH(type1) != MAT_HEIGHT(type2) || MAT_HEIGHT(type1) != MAT_WIDTH(type2)) return 1; break; // transpose
			}
			str_add(&glsl_shader->src, "_"); 
			str_add_ui(&glsl_shader->src, id1);
			ADD_IDX(1);
			str_add(&glsl_shader->src, " = ");
			switch(op_byte) {
				case 0x28: str_add(&glsl_shader->src, "inverse("); break;
				case 0x29: str_add(&glsl_shader->src, "determinant("); break;
				case 0x2A: str_add(&glsl_shader->src, "transpose("); break;
			}
			str_add(&glsl_shader->src, "_"); 
			str_add_ui(&glsl_shader->src, id2);
			ADD_IDX(2);
			str_add(&glsl_shader->src, ");\n"); 
		} else if(*opcode >= 0x2B && *opcode <= 0x2F) {	// id1 = id2 op id3
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID_WITH_MATVEC_ELEMENT(1,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			READ_ID_WITH_MATVEC_ELEMENT(2,FUNC_DEF_BIT);
			READ_ID_WITH_MATVEC_ELEMENT(3,FUNC_DEF_BIT);
			if(type2 > 20 || type3 > 20) return 1;

			// for any matrices, matrix element must exist
			if(type1 >= 9 && type1 <= 17 && matvec_idx1 > MAT_SIZE(type1)-1) return 1;
			if(type2 >= 9 && type2 <= 17 && matvec_idx2 > MAT_SIZE(type2)-1) return 1;
			if(type3 >= 9 && type3 <= 17 && matvec_idx3 > MAT_SIZE(type3)-1) return 1;
			if(type2 < 9 && matvec_idx2 > VEC_SIZE(type2)-1)	// left operand is full vec; output must be as well
				if(type1 >= 9 || matvec_idx1 <= VEC_SIZE(type1)-1 || VEC_SIZE(type1) != VEC_SIZE(type2)) return 1;	// output not a full vec of same size
			if(type3 < 9 && matvec_idx3 > VEC_SIZE(type3)-1)	// right operand is full vec; left operand must be as well
				if(type2 >= 9 || matvec_idx2 <= VEC_SIZE(type2)-1 || VEC_SIZE(type2) != VEC_SIZE(type3)) return 1;	// left operand not a full vec of same size

			// note: if type1 is full vec but type2 + type3 are scalars, the resulting scalar is assigned to all elements of the vector. 
			// if type2 is full vec and type3 is scalar, operation is performed componentwise.
			uint8_t types = base_type(type1) | base_type(type2) | base_type(type3);
			if(!(type2 < 9 && matvec_idx2 >= VEC_SIZE(type2)) && type1 < 9 && matvec_idx1 >= VEC_SIZE(type1)) {	// left operand not full vec but output is	
				// vec = scalar op scalar is not legal in GLSL, so add a result = scalar op scalar line BEFORE assigning anything to the vector
				uint8_t result_type = base_type(type1);
				if(result_type == 0) str_add(&glsl_shader->src, "uint ");
				if(result_type == 1) str_add(&glsl_shader->src, "int ");
				if(result_type  > 1) str_add(&glsl_shader->src, "float ");
				str_add(&glsl_shader->src, "result");
				str_add_ui(&glsl_shader->src, n_result_scalars);
				str_add(&glsl_shader->src, " = ");
				if(result_type == 0) str_add(&glsl_shader->src, "uint(");
				if(result_type == 1) str_add(&glsl_shader->src, "int(");
				if(result_type  > 1) str_add(&glsl_shader->src, "float(");
				// below, insert operations on the scalars into the result# = type(...)
				if(op_byte == 0x2F) {
					str_add(&glsl_shader->src, "pow(");
					types = 2; // the scalars passed as argument to GLSL's pow() must be cast to float
				}
				str_add_typecast(&glsl_shader->src, 0, 0, types);
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id2);
				ADD_IDX(2);
				if(type2 < 9) str_add_vec_idx(&glsl_shader->src, matvec_idx2);	// left operand vector element
				if(type2 >= 9 && type2 <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type2), MAT_HEIGHT(type2), matvec_idx2); // left operand matrix element
				str_add(&glsl_shader->src, ")");
				str_add_operation(&glsl_shader->src, op_byte-0x2B);
				str_add_typecast(&glsl_shader->src, 0, 0, types);
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id3);
				ADD_IDX(3);
				if(type3 < 9) str_add_vec_idx(&glsl_shader->src, matvec_idx3);	// right operand vector element
				if(type3 >= 9 && type3 <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type3), MAT_HEIGHT(type3), matvec_idx3); // right operand matrix element
				if(op_byte == 0x2F) str_add(&glsl_shader->src, ")");
				str_add(&glsl_shader->src, "));\n");
			}		
			if(!(type2 < 9 && matvec_idx2 >= VEC_SIZE(type2)) && type1 < 9 && matvec_idx1 >= VEC_SIZE(type1)) {	// left operand not full vec but output is
				// vec = scalar op scalar is not legal in GLSL, so add vec = vec(result,result,...) (result has been defined on the line prior to the current)
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id1);
				ADD_IDX(1);
				if(type1 < 9 && matvec_idx1 < VEC_SIZE(type1)) str_add_vec_idx(&glsl_shader->src, matvec_idx1);	// output to specific vector element
				if(type1 >= 9 && type1 <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type1), MAT_HEIGHT(type1), matvec_idx1); // output to specific matrix element
				str_add(&glsl_shader->src, " = ");
				str_add_type(&glsl_shader->src, type1);
				str_add(&glsl_shader->src, "(");
				for(uint32_t i = 0; i < VEC_SIZE(type1); i++) {
					str_add(&glsl_shader->src, "result");
					str_add_ui(&glsl_shader->src, n_result_scalars);
					if(i != VEC_SIZE(type1)-1) str_add(&glsl_shader->src, ", ");
				}
				str_add(&glsl_shader->src, ");\n");
				n_result_scalars++;
				continue;
			}
			// for scalar = scalar op scalar, vector = vector op vector, or vector = vector op scalar
			// IF VECTOR = VECTOR OP SCALAR AND OP_BYTE == 0x2F, LOOP ALL OF THIS; MATVEC_IDX1 AND MATVEC_IDX2 WILL BE CYCLED FROM 0 TO VEC_SIZE-1
			uint8_t loop_count = 1;
			if(!(type3 < 9 && matvec_idx3 > VEC_SIZE(type3)-1) // right operand is not full vector
				&& type2 < 9 && matvec_idx2 >= VEC_SIZE(type2) // left operand is full vector
				&& op_byte == 0x2F) { // exponentiation
				matvec_idx1 = 0;
				matvec_idx2 = 0;
				loop_count = VEC_SIZE(type1);
			}
			for(uint32_t i = 0; i < loop_count; i++) {
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id1);
				ADD_IDX(1);
				if(type1 < 9 && matvec_idx1 < VEC_SIZE(type1)) str_add_vec_idx(&glsl_shader->src, matvec_idx1);	// output to specific vector element
				if(type1 >= 9 && type1 <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type1), MAT_HEIGHT(type1), matvec_idx1); // output to specific matrix element
				str_add(&glsl_shader->src, " = ");
				str_add_typecast(&glsl_shader->src, type1<9 && matvec_idx1 >= VEC_SIZE(type1), VEC_SIZE(type1), base_type(type1));
				if(op_byte == 0x2F) {
					str_add(&glsl_shader->src, "pow(");
					types = 2; // the 2 vectors passed as argument to GLSL's pow() must be cast to float vectors (pow(vector,scalar) was translated above)
				}
				str_add_typecast(&glsl_shader->src, type2<9 && matvec_idx2 >= VEC_SIZE(type2), VEC_SIZE(type2), types);
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id2);
				ADD_IDX(2);
				if(type2 < 9 && matvec_idx2 < VEC_SIZE(type2)) str_add_vec_idx(&glsl_shader->src, matvec_idx2);	// left operand vector element
				if(type2 >= 9 && type2 <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type2), MAT_HEIGHT(type2), matvec_idx2); // left operand matrix element
				str_add(&glsl_shader->src, ")");
				str_add_operation(&glsl_shader->src, op_byte-0x2B);
				str_add_typecast(&glsl_shader->src, type3<9 && matvec_idx3 >= VEC_SIZE(type3), VEC_SIZE(type3), types);
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id3);
				ADD_IDX(3);
				if(type3 < 9 && matvec_idx3 < VEC_SIZE(type3)) str_add_vec_idx(&glsl_shader->src, matvec_idx3);	// right operand vector element
				if(type3 >= 9 && type3 <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type3), MAT_HEIGHT(type3), matvec_idx3); // right operand matrix element
				str_add(&glsl_shader->src, "))");
				if(op_byte == 0x2F) str_add(&glsl_shader->src, ")");
				str_add(&glsl_shader->src, ";\n");
				if(loop_count > 1) matvec_idx1++, matvec_idx2++;
			}
		} else if(*opcode >= 0x30 && *opcode <= 0x39) { // id1 = id2 op constant
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID_WITH_MATVEC_ELEMENT(1,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			READ_ID_WITH_MATVEC_ELEMENT(2,FUNC_DEF_BIT);
			uint32_t constant = READ(opcode,4);
			opcode += 4;
			if(type2 > 20) return 1;

			// for any matrices, matrix element must exist
			if(type1 >= 9 && type1 <= 17 && matvec_idx1 > MAT_SIZE(type1)-1) return 1;
			if(type2 >= 9 && type2 <= 17 && matvec_idx2 > MAT_SIZE(type2)-1) return 1;

			if(type2 < 9 && matvec_idx2 > VEC_SIZE(type2)-1)	// left operand is full vec; output must be as well
				if(type1 >= 9 || matvec_idx1 <= VEC_SIZE(type1)-1 || VEC_SIZE(type1) != VEC_SIZE(type2)) return 1;	// output not a full vec of same size

			// note: if type1 is full vec but type2 is a scalar, the resulting scalar is assigned to all elements of the vector. 
			// if type2 is full vec, operation is performed componentwise.
			uint8_t types = base_type(type1) | base_type(type2);
			if(!(type2 < 9 && matvec_idx2 >= VEC_SIZE(type2)) && type1 < 9 && matvec_idx1 >= VEC_SIZE(type1)) {	// left operand not full vec but output is	
				// vec = scalar op scalar is not legal in GLSL, so add a result = scalar op scalar line BEFORE assigning anything to the vector
				uint8_t result_type = base_type(type1);
				if(result_type == 0) str_add(&glsl_shader->src, "uint ");
				if(result_type == 1) str_add(&glsl_shader->src, "int ");
				if(result_type  > 1) str_add(&glsl_shader->src, "float ");
				str_add(&glsl_shader->src, "result");
				str_add_ui(&glsl_shader->src, n_result_scalars);
				str_add(&glsl_shader->src, " = ");
				if(result_type == 0) str_add(&glsl_shader->src, "uint(");
				if(result_type == 1) str_add(&glsl_shader->src, "int(");
				if(result_type  > 1) str_add(&glsl_shader->src, "float(");
				// below, insert operations on the scalars into the result# = type(...)
				if(op_byte == 0x34 || op_byte == 0x39) {
					str_add(&glsl_shader->src, "pow(");
					types = 2; // the scalars passed as argument to GLSL's pow() must be cast to float
				}
				if(op_byte >= 0x35) {
					str_add_typecast(&glsl_shader->src, 0, 0, types);
					str_add_constant(&glsl_shader->src, constant, base_type(type2));
					str_add(&glsl_shader->src, ")");
					str_add_operation(&glsl_shader->src, op_byte-0x35);
				}
				str_add_typecast(&glsl_shader->src, 0, 0, types);
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id2);
				ADD_IDX(2);
				if(type2 < 9) str_add_vec_idx(&glsl_shader->src, matvec_idx2);	// left operand vector element
				if(type2 >= 9 && type2 <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type2), MAT_HEIGHT(type2), matvec_idx2); // left operand matrix element
				str_add(&glsl_shader->src, ")");
				if(op_byte <= 0x34) {
					str_add_operation(&glsl_shader->src, op_byte-0x30);
					str_add_typecast(&glsl_shader->src, 0, 0, types);
					str_add_constant(&glsl_shader->src, constant, base_type(type2));
					str_add(&glsl_shader->src, ")");
				}
				if(op_byte == 0x34 || op_byte == 0x39) str_add(&glsl_shader->src, ")");
				str_add(&glsl_shader->src, ");\n");
			}		
			if(!(type2 < 9 && matvec_idx2 >= VEC_SIZE(type2)) && type1 < 9 && matvec_idx1 >= VEC_SIZE(type1)) {	// left operand not full vec but output is
				// vec = scalar op scalar is not legal in GLSL, so add vec = vec(result,result,...) (result has been defined on the line prior to the current)
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id1);
				ADD_IDX(1);
				if(type1 < 9 && matvec_idx1 < VEC_SIZE(type1)) str_add_vec_idx(&glsl_shader->src, matvec_idx1);	// output to specific vector element
				if(type1 >= 9 && type1 <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type1), MAT_HEIGHT(type1), matvec_idx1); // output to specific matrix element
				str_add(&glsl_shader->src, " = ");
				str_add_type(&glsl_shader->src, type1);
				str_add(&glsl_shader->src, "(");
				for(uint32_t i = 0; i < VEC_SIZE(type1); i++) {
					str_add(&glsl_shader->src, "result");
					str_add_ui(&glsl_shader->src, n_result_scalars);
					if(i != VEC_SIZE(type1)-1) str_add(&glsl_shader->src, ", ");
				}
				str_add(&glsl_shader->src, ");\n");
				n_result_scalars++;
				continue;
			}
			// for scalar = scalar op scalar or vector = vector op scalar
			// IF VECTOR = VECTOR OP SCALAR AND OP_BYTE == 0x2F, LOOP ALL OF THIS; MATVEC_IDX1 AND MATVEC_IDX2 WILL BE CYCLED FROM 0 TO VEC_SIZE-1
			uint8_t loop_count = 1;
			if(type2 < 9 && matvec_idx2 >= VEC_SIZE(type2) // left operand is full vector
				&& op_byte == 0x2F) { // exponentiation
				matvec_idx1 = 0;
				matvec_idx2 = 0;
				loop_count = VEC_SIZE(type1);
			}
			for(uint32_t i = 0; i < loop_count; i++) {
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id1);
				ADD_IDX(1);
				if(type1 < 9 && matvec_idx1 < VEC_SIZE(type1)) str_add_vec_idx(&glsl_shader->src, matvec_idx1);	// output to specific vector element
				if(type1 >= 9 && type1 <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type1), MAT_HEIGHT(type1), matvec_idx1); // output to specific matrix element
				str_add(&glsl_shader->src, " = ");
					
				str_add_typecast(&glsl_shader->src, type1<9 && matvec_idx1 >= VEC_SIZE(type1), VEC_SIZE(type1), base_type(type1));
				if(op_byte == 0x34 || op_byte == 0x39) {
					str_add(&glsl_shader->src, "pow(");
					types = 2; // the 2 vectors passed as argument to GLSL's pow() must be cast to float vectors (pow(vector,scalar) was translated above)
				}
				if(op_byte >= 0x35) {
					str_add_typecast(&glsl_shader->src, 0, 0, types);
					str_add_constant(&glsl_shader->src, constant, base_type(type2));
					str_add(&glsl_shader->src, ")");
					str_add_operation(&glsl_shader->src, op_byte-0x35);
				}
				str_add_typecast(&glsl_shader->src, type2<9 && matvec_idx2 >= VEC_SIZE(type2), VEC_SIZE(type2), types);
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id2);
				ADD_IDX(2);
				if(type2 < 9 && matvec_idx2 < VEC_SIZE(type2)) str_add_vec_idx(&glsl_shader->src, matvec_idx2);	// left operand vector element
				if(type2 >= 9 && type2 <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type2), MAT_HEIGHT(type2), matvec_idx2); // left operand matrix element
				str_add(&glsl_shader->src, ")");
				if(op_byte <= 0x34) {
					str_add_operation(&glsl_shader->src, op_byte-0x30);
					str_add_typecast(&glsl_shader->src, 0, 0, types);
					str_add_constant(&glsl_shader->src, constant, base_type(type2));
					str_add(&glsl_shader->src, ")");
				}
				if(op_byte == 0x34 || op_byte == 0x39) str_add(&glsl_shader->src, ")");
				str_add(&glsl_shader->src, ");\n");
				if(loop_count > 1) matvec_idx1++, matvec_idx2++;
			}
		} else if(*opcode >= 0x3A && *opcode <= 0x4C) { // scalar operations
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID_WITH_MATVEC_ELEMENT(,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			if(type < 9 && matvec_idx > VEC_SIZE(type)-1) return 1; // vector, but vector element does not exist
			if(type >= 9 && type <= 17 && matvec_idx > MAT_SIZE(type)-1) return 1; // matrix, but matrix element does not exist
			if(base_type(type) == 0) return 1; // there are no scalar operations for unsigned int scalars
			if(base_type(type) != 2 && op_byte > 0x3B) return 1; // scalar operations w/ opcode > 0x3B can only be performed on float scalars 

			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			if(type < 9) str_add_vec_idx(&glsl_shader->src, matvec_idx);	// vector element
			if(type >= 9 && type <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type), MAT_HEIGHT(type), matvec_idx); // matrix element
			str_add(&glsl_shader->src, " = ");
			switch(op_byte) {
				case 0x3A: str_add(&glsl_shader->src, "-");			break; case 0x3B: str_add(&glsl_shader->src, "abs(");	break;
				case 0x3C: str_add(&glsl_shader->src, "1./"); 		break; case 0x3D: str_add(&glsl_shader->src, "floor(");	break;
				case 0x3E: str_add(&glsl_shader->src, "ceil(");		break; case 0x3F: str_add(&glsl_shader->src, "tan(");	break;
				case 0x40: str_add(&glsl_shader->src, "sin(");	 	break; case 0x41: str_add(&glsl_shader->src, "cos(");	break;
				case 0x42: str_add(&glsl_shader->src, "atan(");  	break; case 0x43: str_add(&glsl_shader->src, "asin(");	break;
				case 0x44: str_add(&glsl_shader->src, "acos(");  	break; case 0x45: str_add(&glsl_shader->src, "tanh(");	break;
				case 0x46: str_add(&glsl_shader->src, "sinh(");  	break; case 0x47: str_add(&glsl_shader->src, "cosh(");	break;
				case 0x48: str_add(&glsl_shader->src, "atanh("); 	break; case 0x49: str_add(&glsl_shader->src, "asinh(");	break;
				case 0x4A: str_add(&glsl_shader->src, "acosh("); 	break; case 0x4B: str_add(&glsl_shader->src, "log(");	break;
				case 0x4C: str_add(&glsl_shader->src, "log2(");  	break;
			}
			str_add(&glsl_shader->src, "_"); 
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			if(type < 9) str_add_vec_idx(&glsl_shader->src, matvec_idx);	// vector element
			if(type >= 9 && type <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type), MAT_HEIGHT(type), matvec_idx); // matrix element
			if(op_byte != 0x3A && op_byte != 0x3C) str_add(&glsl_shader->src, ")");
			str_add(&glsl_shader->src, ";\n");
		} else if(*opcode == 0x4D || *opcode == 0x4E) { // vector-vector operations
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID(1,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			READ_ID(2,FUNC_DEF_BIT);
			READ_ID(3,FUNC_DEF_BIT);
			// vector-vector cross must all be 3 component float vectors; dot can be float vectors, with output of type float (scalar)
			if(op_byte == 0x4D) // cross prod
				if(type1 != 1 || type2 != 1 || type3 != 1) return 1; // if any not type vec3
			if(op_byte == 0x4E) { // dot prod
				if(type1 != 18 || type2 > 2 || type3 > 2) return 1; // output not float or any operands not a float vector
				if(type2 != type3) return 1; // operands are vectors of different sizes
			}
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id1);
			ADD_IDX(1);
			str_add(&glsl_shader->src, " = ");
			switch(op_byte) {
				case 0x4D: str_add(&glsl_shader->src, "dot("); break;
				case 0x4E: str_add(&glsl_shader->src, "cross("); break;
			}
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id2);
			ADD_IDX(2);
			str_add(&glsl_shader->src, ", _");
			str_add_ui(&glsl_shader->src, id3);
			ADD_IDX(3);
			str_add(&glsl_shader->src, ");\n");
		} else if(*opcode == 0x4F) { // matrix-vector multiplication
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID(1,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			READ_ID(2,FUNC_DEF_BIT);
			READ_ID(3,FUNC_DEF_BIT);
			// vector-matrix mult. right operand must be float vector with size = height of left operand matrix, and output must be float vector with = size too
			if(type1 > 2 || type1 != type3) return 1; // output or right operand not float vector/not of same size
			if(type2 < 9 || type2 > 17) return 1; // left operand not a matrix
			if(VEC_SIZE(type1) != MAT_HEIGHT(type2)) return 1; // vectors not same size as height of matrix
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id1);
			ADD_IDX(1);
			str_add(&glsl_shader->src, " = ");
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id2);
			ADD_IDX(2);
			str_add(&glsl_shader->src, " * _");
			str_add_ui(&glsl_shader->src, id3);
			ADD_IDX(3);
			str_add(&glsl_shader->src, ";\n");
		} else if(*opcode == 0x50) { // matrix-matrix multiplication
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID(1,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			READ_ID(2,FUNC_DEF_BIT);
			READ_ID(3,FUNC_DEF_BIT);
			// matrix-matrix mult. left operand width must be = height of right operand
			// height of output must be = height of left operand and width of output must be = width of right operand
			if(type1 < 9 || type1 > 17 || type2 < 9 || type2 > 17 || type3 < 9 || type3 > 17) return 1;
			if(MAT_WIDTH(type2) != MAT_HEIGHT(type3)) return 1; // mult. is undefined
			if(MAT_HEIGHT(type1) != MAT_HEIGHT(type2) || MAT_WIDTH(type1) != MAT_WIDTH(type3)) return 1; // mult. is undefined
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id1);
			ADD_IDX(1);
			str_add(&glsl_shader->src, " = ");
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id2);
			ADD_IDX(2);
			str_add(&glsl_shader->src, " * _");
			str_add_ui(&glsl_shader->src, id3);
			ADD_IDX(3);
			str_add(&glsl_shader->src, ";\n");
		} else if(*opcode == 0x51) { // vector swizzle
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID(,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			uint8_t swizzle_pattern = READ(opcode,1);
			opcode++;
			if(type >= 9) return 1; // not a vector type
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			str_add(&glsl_shader->src, " = ");
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			str_add(&glsl_shader->src, ".");
			for(uint32_t i = 0; i < VEC_SIZE(type); i++) {
				uint8_t component = (swizzle_pattern&(3<<(i*2))) >> (i*2); // 0-3
				if(component > VEC_SIZE(type)-1) return 1; // component not in vector 
				switch(component) {
					case 0: str_add(&glsl_shader->src, "x"); break;
					case 1: str_add(&glsl_shader->src, "y"); break;
					case 2: str_add(&glsl_shader->src, "z"); break;
					case 3: str_add(&glsl_shader->src, "w"); break;
				}
			}
			str_add(&glsl_shader->src, ";\n");
		} else if(*opcode == 0x52) { // assignment of constant
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID_WITH_MATVEC_ELEMENT(,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			uint32_t constant = READ(opcode,4);
			opcode += 4;
			if(type >= 9 && type <= 17 && matvec_idx > MAT_SIZE(type)-1) return 1; // matrix element must exist
			if(type < 9 && matvec_idx > VEC_SIZE(type)-1) return 1; // vector element must exist
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			if(type < 9) str_add_vec_idx(&glsl_shader->src, matvec_idx);	// output to specific vector element
			if(type >= 9 && type <= 17) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type), MAT_HEIGHT(type), matvec_idx); // output to specific matrix element
			str_add(&glsl_shader->src, " = ");
			str_add_constant(&glsl_shader->src, constant, base_type(type));
			str_add(&glsl_shader->src, ";\n");
		} else if(*opcode == 0x53) { // assignment of an array of constants
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			if(READ(opcode+2,2) == 65535) return 1;
			READ_ID(,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|OUT_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only, and out attrib non-array; exclude in search
			if(type <= 17) return 1; // cannot be matrix or vector
			uint16_t start = arr_idx;
			uint8_t val_count = READ(opcode,1)+1;
			uint32_t values[val_count];
			if(elcount != 0 && start+val_count > elcount) return 1; // if would exceed end of array (elcount is 0 for unsized arrays)
			opcode++;
			for(uint32_t i = 0; i < val_count; i++) {
				values[i] = READ(opcode,4);
				opcode += 4;
			}
			for(uint32_t i = 0; i < val_count; i++) {
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id);
				ADD_IDX();
				str_add(&glsl_shader->src, " = ");
				str_add_constant(&glsl_shader->src, values[i], base_type(type));
				str_add(&glsl_shader->src, ";\n");
			}
		} else if(*opcode == 0x54) { // id1 = id2
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID_WITH_MATVEC_ELEMENT(1,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			READ_ID_WITH_MATVEC_ELEMENT(2,FUNC_DEF_BIT);
			if(type2 > 20) return 1;
			if(type1 >= 9 && type1 <= 17 && matvec_idx1 > MAT_SIZE(type1)-1) // left operand is full matrix; right operand must be as well
				if(type1 != type2 || matvec_idx2 < MAT_SIZE(type2)) return 1; // right operand not a full matrix of same dimensions
			if(type2 >= 9 && type2 <= 17 && matvec_idx2 > MAT_SIZE(type2)-1) // right operand is full matrix; left operand must be as well
				if(type1 != type2 || matvec_idx1 < MAT_SIZE(type1)) return 1; // left operand not a full matrix of same dimensions
			if(type1 < 9 && matvec_idx1 > VEC_SIZE(type1)-1)	// left operand is full vec; right operand must be as well
				if(type2 >= 9 || matvec_idx2 <= VEC_SIZE(type2)-1 || VEC_SIZE(type1) != VEC_SIZE(type2)) return 1;	// output not a full vec of same size
			if(type2 < 9 && matvec_idx2 > VEC_SIZE(type2)-1)	// right operand is full vec; left operand must be as well
				if(type1 >= 9 || matvec_idx1 <= VEC_SIZE(type1)-1 || VEC_SIZE(type1) != VEC_SIZE(type2)) return 1;	// left operand not a full vec of same size

			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id1);
			ADD_IDX(1);
			if(type1 < 9 && matvec_idx1 < VEC_SIZE(type1)) str_add_vec_idx(&glsl_shader->src, matvec_idx1);
			if(type1 >= 9 && type1 <= 17 && matvec_idx1 < MAT_SIZE(type1)) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type1), MAT_HEIGHT(type1), matvec_idx1);
			str_add(&glsl_shader->src, " = ");
			str_add_typecast(&glsl_shader->src, type1<9 && matvec_idx1 >= VEC_SIZE(type1), VEC_SIZE(type1), base_type(type1));
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id2);
			ADD_IDX(2);
			if(type2 < 9 && matvec_idx2 < VEC_SIZE(type2)) str_add_vec_idx(&glsl_shader->src, matvec_idx2);
			if(type2 >= 9 && type2 <= 17 && matvec_idx1 < MAT_SIZE(type2)) str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type2), MAT_HEIGHT(type2), matvec_idx2);
			str_add(&glsl_shader->src, ");\n");
		} else if(*opcode == 0x55) { // vertex position output
			if(shader_type != 0) return 1; // this can only be present in vertex shader
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID(,FUNC_DEF_BIT);
			if(type != 2) return 1; // must be vec4
			str_add(&glsl_shader->src, "gl_Position = vec4(");
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			str_add(&glsl_shader->src, ".x, -_");
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			str_add(&glsl_shader->src, ".y, _");
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			str_add(&glsl_shader->src, ".z, _");			// NDC depth is in range [0,1] but will be adjusted to OpenGL's [-1,1] range
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			str_add(&glsl_shader->src, ".w);\n");
			str_add(&glsl_shader->src, "gl_Position.z = ((gl_Position.z/gl_Position.w)*2-1)*gl_Position.w;\n");
		} else if(*opcode == 0x56) { // depth output
			if(shader_type != 1) return 1; // this can only be present in pixel shader
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID(,FUNC_DEF_BIT);
			if(type != 18) return 1; // must be float
			str_add(&glsl_shader->src, "gl_FragDepth = ");
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			str_add(&glsl_shader->src, ";\n");
			modified_frag_depth = 1;
		} else if(*opcode == 0x57) { // instance ID getter
			if(shader_type != 0) return 1; // this can only be present in vertex shader
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID(,FUNC_DEF_BIT);
			if(type != 19 && type != 20) return 1; // must be int or uint
			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id);
			ADD_IDX();
			if(type == 19) str_add(&glsl_shader->src, "= gl_InstanceID;\n");
			else str_add(&glsl_shader->src, "= uint(gl_InstanceID);\n");
		} else if(*opcode == 0x58); // image read
		else if(*opcode == 0x59); // image write
		else if(*opcode == 0x5A); // image dimensions
		else if(*opcode >= 0x5B && *opcode <= 0x5D) { // sample using explicit LOD (0x5B), auto LOD (0x5C), or texel coords (0x5D)
			if(*opcode == 0x5C && (shader_type != 1 || scope_type != 1 || scope_level != 1)) return 1; // can only be on level 1 of pixel shader's main function
			opcode++;
			READ_ID(1,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			READ_ID(2,ALL_DEF_BIT&(~UNIF_DEF_BIT)); // must be uniform
			READ_ID(3,FUNC_DEF_BIT);
			READ_ID_WITH_MATVEC_ELEMENT(4,FUNC_DEF_BIT);
			if(op_byte != 0x5D && (type3 != 0 || type4 != 18)) return 1;	// texture sample requires vec2 as texture coords, float for LOD/bias
			else if(op_byte == 0x5D && (type3 != 3 || type4 != 20)) return 1;	// texel sample requires ivec2 as texel coords, uint for LOD
			if((type1 != 2 && type1 != 5 && type1 != 8) || type2 < 21 || type2 > 23) return 1; // output must be u/i/vec4, type2 u/i/sampler
			if(!(arr_idx2 >= 0 || arr_idx2 == IDX_TYPE_LOOP)) return 1;	// error if sampler array index isn't either a constant or the current loop iteration

			uint32_t it_count = 1;
			if(arr_idx2 == IDX_TYPE_LOOP) it_count = level_iterations[scope_level-1];	// if in loop
			for(uint32_t i = 0; i < it_count; i++) {
				if(arr_idx2 == IDX_TYPE_LOOP) {
					str_add(&glsl_shader->src, "if(");
					str_add_iterator(&glsl_shader->src, scope_level-1);
					str_add(&glsl_shader->src, " == ");
					str_add_ui(&glsl_shader->src, i);
					str_add(&glsl_shader->src, ") ");
				}
				str_add(&glsl_shader->src, "_");
				str_add_ui(&glsl_shader->src, id1);
				ADD_IDX(1);
				if(op_byte == 0x5B)			str_add(&glsl_shader->src, " = textureLod(_");
				else if(op_byte == 0x5C)	str_add(&glsl_shader->src, " = texture(_");
				else if(op_byte == 0x5D)	str_add(&glsl_shader->src, " = texelFetch(_");
				str_add_ui(&glsl_shader->src, id2);
				if(arr_idx2 != IDX_TYPE_LOOP) {
					ADD_IDX(2);
				} else {
					str_add(&glsl_shader->src, "[");
					str_add_ui(&glsl_shader->src, i);
					str_add(&glsl_shader->src, "]");
				}
				str_add(&glsl_shader->src, ",_");
				str_add_ui(&glsl_shader->src, id3);
				ADD_IDX(3);
				if(op_byte == 0x5D) str_add(&glsl_shader->src, ",int(_");	// GLSL doesn't implicitly convert uint to int
				else str_add(&glsl_shader->src, ",_");
				str_add_ui(&glsl_shader->src, id4);
				ADD_IDX(4);
				if(type4 < 9) { // vector
					if(matvec_idx4 > VEC_SIZE(type4)-1) return 1; // vector element must exist
					str_add_vec_idx(&glsl_shader->src, matvec_idx4);	// output to specific vector element
				} else if(type4 <= 17) { // matrix
					if(matvec_idx4 > MAT_SIZE(type4)-1) return 1; // matrix element must exist
					str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type4), MAT_HEIGHT(type4), matvec_idx4); // output to specific matrix element
				}
				if(op_byte == 0x5D) str_add(&glsl_shader->src, "));\n");
				else str_add(&glsl_shader->src, ");\n");
			}
		} else if(*opcode == 0x5E) { // texture dimensions
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			opcode++;
			READ_ID(1,ALL_DEF_BIT&(~UNIF_DEF_BIT)); // must be uniform
			READ_ID(2,UNIF_DEF_BIT|IN_ATTR_DEF_BIT|FUNC_DEF_BIT); // uniform + in attrib read-only; exclude in search
			READ_ID(3,FUNC_DEF_BIT);
			if(type1 < 21 || type1 > 23) return 1;	// first id must be a sampler type
			if(type2 != 3) return 1;	// second id must be ivec2
			if(type3 != 19) return 1;	// third id must be int

			str_add(&glsl_shader->src, "_");
			str_add_ui(&glsl_shader->src, id2);
			ADD_IDX(2);
			str_add(&glsl_shader->src, " = textureSize(_");
			str_add_ui(&glsl_shader->src, id1);
			ADD_IDX(1);
			str_add(&glsl_shader->src, ", _");
			str_add_ui(&glsl_shader->src, id3);
			ADD_IDX(3);
			str_add(&glsl_shader->src, ");\n");
		} else if(*opcode >= 0x5F && *opcode <= 0x61) {	// if, else-if, or else
			if((scope_type != 1 && scope_type != 2) || scope_level == 0) return 1; // must be in a function
			if(scope_level == 8) return 1;	// can't open a conditional on level 8
			scope_level++; // go up a level
			if(*opcode != 0x5F && !level_allow_if_extension[scope_level-1]) return 1;	// if an else-if or else opcode but no previous if
			if(*opcode == 0x5F) level_allow_if_extension[scope_level-1] = 1;	// if opcode; enable if extensions for this level
			if(*opcode == 0x61) level_allow_if_extension[scope_level-1] = 0;	// else opcode; disable if extensions for this level
			level_status[scope_level-1] = 1;	// set level to conditional status
			if(*opcode == 0x5F) str_add(&glsl_shader->src, "if(");
			else if(*opcode == 0x60) str_add(&glsl_shader->src, "else if(");
			else if(*opcode == 0x61) str_add(&glsl_shader->src, "else ");
			uint8_t branch_type = *opcode;
			opcode++;
			while(opcode <= end) {
				if(*opcode == 0xE) {	// open conditional body (end of condition list)
					if(branch_type != 0x61) str_add(&glsl_shader->src, ") ");
					break;
				} else if(branch_type == 0x61) return 1;			// else shouldn't have a conditions list
				else {		// value cond value
					uint8_t is_id_1 = 0, is_id_2 = 0;
					uint32_t constant_1 = 0;
					uint32_t constant_2 = 0;
					uint16_t id1, idx_id1, multiplier1, id2, idx_id2, multiplier2;
					uint8_t is_arr1, type1, elcount1, matvec_idx1, is_arr2, type2, elcount2, matvec_idx2;
					int32_t offset1, arr_idx1, offset2, arr_idx2;

					if(*opcode == 0) {			// first operand is a constant
						opcode++;
						constant_1 = READ(opcode, 4);
						opcode += 4;
					} else {					// first operand is a variable
						opcode++;
						READ_ID_WITH_MATVEC_ELEMENT(,FUNC_DEF_BIT);
						is_id_1 = 1;
						id1=id, idx_id1=idx_id, multiplier1=multiplier;
						is_arr1=is_arr, type1=type, elcount1=elcount, matvec_idx1=matvec_idx;
						offset1=offset, arr_idx1=arr_idx;
					}

					uint8_t cond_op = *opcode;
					if(cond_op < 0x62 || cond_op > 0x67) return 1;	
					opcode++;

					if(*opcode == 0) {			// second operand is a constant
						opcode++;
						constant_2 = READ(opcode, 4);
						opcode += 4;
					} else {					// second operand is a variable
						opcode++;
						READ_ID_WITH_MATVEC_ELEMENT(,FUNC_DEF_BIT);
						is_id_2 = 1;
						id2=id, idx_id2=idx_id, multiplier2=multiplier;
						is_arr2=is_arr, type2=type, elcount2=elcount, matvec_idx2=matvec_idx;
						offset2=offset, arr_idx2=arr_idx;
					}

					uint8_t types = 2;	// default to float

					if(is_id_1) types = base_type(type1);
					if(is_id_2) types = base_type(type2);
					if(is_id_1 && is_id_2) types = base_type(type1) | base_type(type2);

					str_add_typecast(&glsl_shader->src, 0, 0, types);
					if(is_id_1) {
						str_add(&glsl_shader->src, "_");
						str_add_ui(&glsl_shader->src, id1);
						ADD_IDX(1);
						if(type1 < 9) { // vector
							if(matvec_idx1 > VEC_SIZE(type1)-1) return 1; // vector element must exist
							str_add_vec_idx(&glsl_shader->src, matvec_idx1);	// output to specific vector element
						} else if(type1 <= 17) { // matrix
							if(matvec_idx1 > MAT_SIZE(type1)-1) return 1; // matrix element must exist
							str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type1), MAT_HEIGHT(type1), matvec_idx1); // output to specific matrix element
						}
					} else {
						if(types == 0) str_add_ui(&glsl_shader->src, *(uint32_t*)&constant_1);
						else if(types == 1) str_add_i(&glsl_shader->src, *(int32_t*)&constant_1);
						else str_add_f(&glsl_shader->src, *(float*)&constant_1);
					}
					str_add(&glsl_shader->src, ")");

					switch(cond_op) {
						case 0x62: str_add(&glsl_shader->src, " > "); break;
						case 0x63: str_add(&glsl_shader->src, " < "); break;
						case 0x64: str_add(&glsl_shader->src, " <= "); break;
						case 0x65: str_add(&glsl_shader->src, " >= "); break;
						case 0x66: str_add(&glsl_shader->src, " == "); break;
						case 0x67: str_add(&glsl_shader->src, " != "); break;
					}

					str_add_typecast(&glsl_shader->src, 0, 0, types);
					if(is_id_2) {
						str_add(&glsl_shader->src, "_");
						str_add_ui(&glsl_shader->src, id2);
						ADD_IDX(2);
						if(type2 < 9) { // vector
							if(matvec_idx2 > VEC_SIZE(type2)-1) return 1; // vector element must exist
							str_add_vec_idx(&glsl_shader->src, matvec_idx2);	// output to specific vector element
						} else if(type2 <= 17) { // matrix
							if(matvec_idx2 > MAT_SIZE(type2)-1) return 1; // matrix element must exist
							str_add_mat_idx(&glsl_shader->src, MAT_WIDTH(type2), MAT_HEIGHT(type2), matvec_idx2); // output to specific matrix element
						}
					} else {
						if(types == 0) str_add_ui(&glsl_shader->src, *(uint32_t*)&constant_2);
						else if(types == 1) str_add_i(&glsl_shader->src, *(int32_t*)&constant_2);
						else str_add_f(&glsl_shader->src, *(float*)&constant_2);
					}
					str_add(&glsl_shader->src, ")");
				}
				if(*opcode == 0x68) str_add(&glsl_shader->src, " || "), opcode++;
				if(*opcode == 0x69) str_add(&glsl_shader->src, " && "), opcode++;
			}
		} else if(*opcode == 0x6A) { // loop
			if(scope_type != 1 && scope_type != 2) return 1; // must be in a function
			if(scope_level == 8) return 1; // max level exceeded
			uint16_t it_count = READ(opcode+1,2);
			if(!it_count) return 1;		// loop must have at least one iteration
			if(READ(opcode+3,1) != 0x0E) return 1;	// missing loop opener
			opcode += 3;

			str_add(&glsl_shader->src, "for(int ");
			str_add_iterator(&glsl_shader->src, scope_level);
			str_add(&glsl_shader->src, " = 0; ");
			str_add_iterator(&glsl_shader->src, scope_level);
			str_add(&glsl_shader->src, " < ");
			str_add_ui(&glsl_shader->src, it_count);
			str_add(&glsl_shader->src, "; ");
			str_add_iterator(&glsl_shader->src, scope_level);
			str_add(&glsl_shader->src, "++)\n");

			level_status[scope_level] = 2;	// set level's status to in loop
			level_iterations[scope_level] = it_count;	// set level's iteration count
			scope_level++;	// go to next level
		} else return 1; // non-existent opcode encountered
	}
	// by the end of the shader, scope_level and scope_type must both be 1, or else the shader contains unclosed functions or contains no main function
	if(scope_level != 1 || scope_type != 1) return 1;
	str_add(&glsl_shader->src, "}\n");	// close off the main function
	if(modified_frag_depth && shader_type == 1)
		str_insert(&glsl_shader->src, "gl_FragDepth = gl_FragCoord.z;\n", entry_point);		// if a shader contains gl_FragDepth = ..., it must write to the value in all cases

	shader_data->defs = defs;
	shader_data->n_defs = n_defs;

	return 0;
}

typedef struct tbo_t {
	GLint gl_buffer;	// the ID of the GL texture object
	uint32_t n_levels;	// how many levels have already been uploaded to
	uint32_t* level_widths; // width of each level
	uint32_t* level_heights; // height of each level
	uint8_t format;		// the format for this TBO
} tbo_t;

// uploads to a texture level of a TBO, if possible
void upload_texture(tbo_t* tbo, uint32_t level, uint32_t width, uint32_t height, void* data) {
	if(width > max_texture_size || height > max_texture_size) return;
	glBindTexture(GL_TEXTURE_2D, tbo->gl_buffer);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	if(level == tbo->n_levels) {	// next level specified; glTexImage2D
		tbo->level_widths = realloc(tbo->level_widths, sizeof(uint32_t)*(tbo->n_levels+1));
		tbo->level_heights = realloc(tbo->level_heights, sizeof(uint32_t)*(tbo->n_levels+1));
		tbo->level_widths[tbo->n_levels] = width;
		tbo->level_heights[tbo->n_levels] = height;
		tbo->n_levels++;
		switch(tbo->format) {
			case 0: glTexImage2D(GL_TEXTURE_2D, level, GL_R8I, width, height, 0, GL_RED, GL_BYTE, data); break;
			case 1: glTexImage2D(GL_TEXTURE_2D, level, GL_R8UI, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data); break;
			case 2: glTexImage2D(GL_TEXTURE_2D, level, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, data); break;
			case 3: glTexImage2D(GL_TEXTURE_2D, level, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data); break;
			case 4: glTexImage2D(GL_TEXTURE_2D, level, GL_RG8I, width, height, 0, GL_RG, GL_BYTE, data); break;
			case 5: glTexImage2D(GL_TEXTURE_2D, level, GL_RG8UI, width, height, 0, GL_RG, GL_UNSIGNED_BYTE, data); break;
			case 6: glTexImage2D(GL_TEXTURE_2D, level, GL_RG32F, width, height, 0, GL_RG, GL_FLOAT, data); break;
			case 7: glTexImage2D(GL_TEXTURE_2D, level, GL_RG8, width, height, 0, GL_RG, GL_UNSIGNED_BYTE, data); break;
			case 8: glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8I, width, height, 0, GL_RGBA, GL_BYTE, data); break;
			case 9: glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8UI, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data); break;
			case 10: glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, data); break;
			case 11: glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data); break;
			case 12: glTexImage2D(GL_TEXTURE_2D, level, GL_DEPTH_COMPONENT32F, width, height, 0, GL_RED, GL_FLOAT, data); break;
			case 13: glTexImage2D(GL_TEXTURE_2D, level, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, data); break;
		}
	} else if(level < tbo->n_levels) {	// level exists; glTexSubImage2D
		tbo->level_widths[level] = width;
		tbo->level_heights[level] = height;
		switch(tbo->format) {
			case 0: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RED, GL_BYTE, data); break;
			case 1: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, data); break;
			case 2: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RED, GL_FLOAT, data); break;
			case 3: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, data); break;
			case 4: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RG, GL_BYTE, data); break;
			case 5: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RG, GL_UNSIGNED_BYTE, data); break;
			case 6: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RG, GL_FLOAT, data); break;
			case 7: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RG, GL_UNSIGNED_BYTE, data); break;
			case 8: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RGBA, GL_BYTE, data); break;
			case 9: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data); break;
			case 10: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RGBA, GL_FLOAT, data); break;
			case 11: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data); break;
			case 12: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_RED, GL_FLOAT, data); break;
			case 13: glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, data); break;
		}
	} else return;
	tbo->n_levels++;
}

typedef struct fbo_t {
	GLint gl_buffer;
	uint32_t width;
	uint32_t height;
} fbo_t;

typedef struct ubo_t {
	uint8_t* data;
	uint64_t size;
} ubo_t;

typedef struct dbo_t {
	uint8_t* data;
	uint64_t size;
} dbo_t;

typedef struct vao_t {
	uint64_t stride;
	uint16_t* ids;
	uint64_t* offsets;
	uint8_t* formats;
	uint32_t n_attribs;

	GLint* gl_vao_ids; // list of GL VAOs that are related to each VBO object ID in vbo_ids (parallel arrays)
	uint64_t* vbo_ids;
	uint32_t n_vaos; // # of GL VAOs created (really just instances of the same VAO)
} vao_t;

typedef struct vid_data_t {
	uint8_t** frames;
	uint32_t n_frames;
	uint32_t width;
	uint32_t height;
} vid_data_t;

typedef struct object_t {
	cbo_t cbo;
	GLint gl_buffer;	// a GL buffer object that doesn't have a dedicated structre for storing additional information; VBO, IBO, VAO
	tbo_t tbo;
	sbo_t sbo;
	vao_t vao;
	fbo_t fbo;
	ubo_t ubo;
	dbo_t dbo;
	vid_data_t vid_data;
	uint32_t object_id;	// for descriptors; the object pointed to by this descriptor
	uint32_t image_level;	// for image descriptors; the texture level of the TBO whose ID is specified by object_id
	uint8_t min_filter;	// for sampler descriptors
	uint8_t mag_filter;
	uint8_t s_mode;
	uint8_t t_mode;
	shader_t shader;	// shader/shader source object
	desc_set_t dset;	// descriptor set object
	set_layout_t set_layout;	// descriptor set layouts
	pipeline_t pipeline;
	segtable_t segtable; // segment table objects
	uint8_t type;
	uint64_t mapped_address;// the (system memory) address of this object's buffer mapping (if 0, this buffer is not mapped)
	uint8_t deleted;	// whether or not this object has been deleted
	uint64_t privacy_key;	// this object's privacy key
} object_t;

uint64_t new_object() {	// returns the object's ID
	objects = realloc(objects, sizeof(object_t)*(++n_objects));
	return n_objects;
}

// will set segfault bit & return 1 if the range is not fully within
// segments of main memory, unsets and returns 0 otherwise
uint8_t check_segfault(thread_t* thread, uint64_t address, uint64_t n_bytes) {
	uint64_t max_address = address + n_bytes - 1;
	thread->regs[13] |= SR_BIT_SEGFAULT;	// will only be unset if there was no segfault
	if(max_address >= SIZE_MAIN_MEM) return 1;
	if(!thread->segtable_id || (thread->segtable_id && objects[thread->segtable_id-1].segtable.n_segments) == 0) {
		if(!thread->segtable_id && thread->id == 0 && max_address < SIZE_MAIN_MEM) {
			thread->regs[13] &= (~SR_BIT_SEGFAULT); // no segfault
			return 0;
		}
		return 1;	// no segments in segment table, or no segment table and not thread 0; memory access will always segfault
	}
	uint64_t bytes_accessible = 0;
	uint32_t current_segment = 0;
	segtable_t* segtable = &objects[thread->segtable_id-1].segtable;
	while(bytes_accessible != n_bytes && current_segment < segtable->n_segments) {
		// throughout all segments, sum how many bytes in the address range are accessible
		segment_t* segment = &segtable->segments[current_segment];
		if(segment->deleted) { current_segment++; continue; }
		uint64_t seg_end = segment->v_address + segment->length - 1;
		uint64_t min_end = max_address < seg_end ? max_address : seg_end;
		uint64_t max_start = address > segment->v_address ? address : segment->v_address;
		if(min_end >= max_start) // otherwise, current segment does not include any bytes in the address range
			bytes_accessible += min_end-max_start+1;
		current_segment++;
	}
	if(bytes_accessible == n_bytes) thread->regs[13] &= (~SR_BIT_SEGFAULT); // no segfault
	else if(SHOW_SEGFAULT) printf("segmentation fault\n");
	return bytes_accessible != n_bytes;
}

// updates a thread's "current file stream is open" bit in the SR
void update_stream_open(thread_t* thread) {
	uint16_t stream_id = (thread->regs[13] & 0xFFFF0000000ull)>>28;
	if(stream_id && thread->file_streams[stream_id-1]) thread->regs[13] |= 0x4000000;
	else thread->regs[13] &= (~0x4000000ull);
}

uint64_t loadval(uint8_t* a, uint8_t n) {
	switch(n) {
		case 1: return *a; break;
		case 2: return *(uint16_t*)a; break;
		case 3: return *a|(uint64_t)*(a+1)<<8|(uint64_t)*(a+2)<<16; break;
		case 4: return *(uint32_t*)a; break;
		case 5: return *a|(uint64_t)*(a+1)<<8|(uint64_t)*(a+2)<<16|(uint64_t)*(a+3)<<24|(uint64_t)(*(a+4))<<32; break;
		case 6: return *a|(uint64_t)*(a+1)<<8|(uint64_t)*(a+2)<<16|(uint64_t)*(a+3)<<24|(uint64_t)(*(a+4))<<32|(uint64_t)(*(a+5))<<40; break;
		case 7: return *a|(uint64_t)*(a+1)<<8|(uint64_t)*(a+2)<<16|(uint64_t)*(a+3)<<24|(uint64_t)(*(a+4))<<32|(uint64_t)(*(a+5))<<40|(uint64_t)(*(a+6))<<48; break;
		default: return *(uint64_t*)a; break;
	}
}

// reads 1-8 bytes from main memory. make sure to call check_segfault on the read region first.
uint64_t read_main_mem_val(thread_t* thread, uint64_t address, uint8_t n_bytes) {
	if(n_bytes == 0 || n_bytes > 8) return 0;
	uint64_t value = 0;
	uint8_t* val = (uint8_t*)&value;
	uint8_t bytes_read = 0;
	uint32_t current_segment = 0;
	uint64_t max_address = address + n_bytes - 1;
	if(!thread->segtable_id && thread->id == 0)
		return loadval(&memory[address], n_bytes);
	while(bytes_read != n_bytes) {
		if(current_segment >= objects[thread->segtable_id-1].segtable.n_segments) break;
		if(objects[thread->segtable_id-1].segtable.segments[current_segment].deleted) { current_segment++; continue; }
		segment_t* segment = &objects[thread->segtable_id-1].segtable.segments[current_segment];
		uint64_t seg_end = segment->v_address + segment->length - 1;
		uint64_t min_end = max_address < seg_end ? max_address : seg_end;
		uint64_t max_start = address > segment->v_address ? address : segment->v_address;
		if(min_end >= max_start) { // otherwise, current segment does not include any bytes in the address range
			uint64_t bytes_to_read = min_end-max_start+1; // this is how many bytes are accessible in this segment within the virtual address range; can be 1 up to n_bytes
			uint8_t* a = memory+segment->p_address+(max_start==segment->v_address ? 0 : max_start-segment->v_address); // calculate the address to read 'bytes_to_read' number of bytes from;
			// sums offset to current point in segment with the physical address of the segment.
			// bytes_read is how much has already been read, and bytes_to_read is how much to read from address a.
			if(bytes_to_read == n_bytes) {
				switch(bytes_to_read) {
					case 1: return *a; break;
					case 2: return *(uint16_t*)a; break;
					case 4: return *(uint32_t*)a; break;
					case 8: return *(uint64_t*)a; break;
				}
			} 
			uint8_t offset = max_start-address; // max_start-address is the offset into the address range for the first byte that the segment includes
			memmove(val+offset, a, bytes_to_read);
			bytes_read += bytes_to_read;
		}	
		current_segment++;
	}
	return value;
}

// reads bytes from main memory and returns an allocated block of all data. make sure to call check_segfault on the read region first.
// returns a pointer to the read memory
uint8_t* read_main_mem(thread_t* thread, uint64_t address, uint64_t n_bytes) {
	if(n_bytes == 0) return 0;
	uint8_t* data = calloc(1,n_bytes);
	uint64_t bytes_read = 0;
	uint32_t current_segment = 0;
	uint64_t max_address = address + n_bytes - 1;
	if(!thread->segtable_id && thread->id == 0) {
		memcpy(data, memory+address, n_bytes);
		return data;
	}
	while(bytes_read != n_bytes) {
		if(current_segment >= objects[thread->segtable_id-1].segtable.n_segments) break;
		if(objects[thread->segtable_id-1].segtable.segments[current_segment].deleted) { current_segment++; continue; }
		segment_t* segment = &objects[thread->segtable_id-1].segtable.segments[current_segment];
		uint64_t seg_end = segment->v_address + segment->length - 1;
		uint64_t min_end = max_address < seg_end ? max_address : seg_end;
		uint64_t max_start = address > segment->v_address ? address : segment->v_address;
		if(min_end >= max_start) { // otherwise, current segment does not include any bytes in the address range
			uint64_t bytes_to_read = min_end-max_start+1; // this is how many bytes are accessible in this segment within the virtual address range; can be 1 up to n_bytes, at least 1
			uint8_t* a = memory+segment->p_address+(max_start==segment->v_address ? 0 : max_start-segment->v_address); // calculate the address to read 'bytes_to_read' number of bytes from;
			// sums offset to current point in segment with the physical address of the segment.
			// bytes_read is how much has already been read, and bytes_to_read is how much to read from address a.
			memcpy(data+(max_start-address), a, bytes_to_read); // max_start-address is the offset into the the address range for the first byte that the segment includes
			bytes_read += bytes_to_read;
		}	
		current_segment++;
	}
	return data;
}

// writes a value to main memory (1-8 bytes, in little-endian ordering). make sure to call check_segfault on the region being written to first.
void write_main_mem_val(thread_t* thread, uint64_t address, uint64_t value, uint8_t n_bytes) {
	if(n_bytes == 0 || n_bytes > 8) return;
	uint8_t* val = (uint8_t*)&value;
	uint8_t bytes_written = 0;
	uint32_t current_segment = 0;
	uint64_t max_address = address + n_bytes - 1;
	if(!thread->segtable_id && thread->id == 0) {
		uint8_t* a = memory+address;
		switch(n_bytes) {
			case 1: *a = value; break;
			case 2: *(uint16_t*)a = value; break;
			case 4: *(uint32_t*)a = value; break;
			case 8: *(uint64_t*)a = value; break;
			default: memmove(a, val, n_bytes);
		}
		return;
	}
	while(bytes_written != n_bytes) {
		if(current_segment >= objects[thread->segtable_id-1].segtable.n_segments) break;
		if(objects[thread->segtable_id-1].segtable.segments[current_segment].deleted) { current_segment++; continue; }
		segment_t* segment = &objects[thread->segtable_id-1].segtable.segments[current_segment];
		uint64_t seg_end = segment->v_address + segment->length - 1;
		uint64_t min_end = max_address < seg_end ? max_address : seg_end;
		uint64_t max_start = address > segment->v_address ? address : segment->v_address;
		if(min_end >= max_start) { // otherwise, current segment does not include any bytes in the address range
			uint64_t bytes_to_write = min_end-max_start+1; // this is how many bytes are accessible in this segment within the virtual address range; can be 1 up to n_bytes
			uint8_t* a = memory+segment->p_address+(max_start==segment->v_address ? 0 : max_start-segment->v_address); // calculate the address to write 'bytes_to_write' number of bytes to;
			// sums offset to current point in segment with the physical address of the segment.
			// bytes_written is how much has already been written, and bytes_to_write is how much to write to address a.
			if(bytes_to_write == n_bytes) {	// handle the nice powers of 2 on little-endian systems
				switch(bytes_to_write) {
					case 1: *a = value; return; break;
					case 2: *(uint16_t*)a = value; return; break;
					case 4: *(uint32_t*)a = value; return; break;
					case 8: *(uint64_t*)a = value; return; break;
				}
			}
			uint8_t offset = max_start-address; // max_start-address is the offset into the address range for the first byte that the segment includes
			memmove(a, val+offset, bytes_to_write);
			bytes_written += bytes_to_write;
		}	
		current_segment++;
	}
}

// writes bytes to main memory. make sure to call check_segfault on the region being written to first.
void write_main_mem(thread_t* thread, uint64_t address, uint8_t* data, uint64_t n_bytes) {
	if(n_bytes == 0) return;
	uint64_t bytes_written = 0;
	uint32_t current_segment = 0;
	uint64_t max_address = address + n_bytes - 1;
	if(!thread->segtable_id && thread->id == 0) {
		memmove(memory+address, data, n_bytes);
		return;
	}
	while(bytes_written != n_bytes) {
		if(current_segment >= objects[thread->segtable_id-1].segtable.n_segments) break;
		if(objects[thread->segtable_id-1].segtable.segments[current_segment].deleted) { current_segment++; continue; }
		segment_t* segment = &objects[thread->segtable_id-1].segtable.segments[current_segment];
		uint64_t seg_end = segment->v_address + segment->length - 1;
		uint64_t min_end = max_address < seg_end ? max_address : seg_end;
		uint64_t max_start = address > segment->v_address ? address : segment->v_address;
		if(min_end >= max_start) { // otherwise, current segment does not include any bytes in the address range
			uint64_t bytes_to_write = min_end-max_start+1; // this is how many bytes are accessible in this segment within the virtual address range; can be 1 up to n_bytes
			uint8_t* a = memory+segment->p_address+(max_start==segment->v_address ? 0 : max_start-segment->v_address); // calculate the address to write 'bytes_to_write' number of bytes to;
			// sums offset to current point in segment with the physical address of the segment.
			// bytes_written is how much has already been written, and bytes_to_write is how much to write to address a.
			memmove(a, data+(max_start-address), bytes_to_write); // max_start-address is the offset into the the address range for the first byte that the segment includes
			bytes_written += bytes_to_write;
		}	
		current_segment++;
	}
}

// binds a VBO + its associated VAO, creating a new one as necessary.
void bind_vbo(vao_t* vao, uint64_t vbo_id) {
	object_t* vbo = &objects[vbo_id-1]; // assumes VBO object exists and is not deleted.
	glBindBuffer(GL_ARRAY_BUFFER, vbo->gl_buffer);
	// find existing VAO parallel to vbo_id, otherwise create a new VAO and bind that
	for(uint32_t i = 0; i < vao->n_vaos; i++)
		if(vao->vbo_ids[i] == vbo_id) {
			glBindVertexArray(vao->gl_vao_ids[i]);
			return;
		}
	vao->gl_vao_ids = realloc(vao->gl_vao_ids, sizeof(GLint)*(vao->n_vaos+1));
	vao->vbo_ids = realloc(vao->vbo_ids, sizeof(GLint)*(vao->n_vaos+1));
	vao->vbo_ids[vao->n_vaos] = vbo_id;

	GLint gl_id = 0;
	glGenVertexArrays(1,&gl_id);
	glBindVertexArray(gl_id);
	vao->gl_vao_ids[vao->n_vaos] = gl_id;
	for(uint32_t i = 0; i < vao->n_attribs; i++) {
		glEnableVertexAttribArray(vao->ids[i]);
		switch(vao->formats[i]) {
			case 0: glVertexAttribPointer(vao->ids[i], 1, GL_FLOAT, GL_FALSE, vao->stride, (void*)vao->offsets[i]); break;
			case 1: glVertexAttribPointer(vao->ids[i], 2, GL_FLOAT, GL_FALSE, vao->stride, (void*)vao->offsets[i]); break;
			case 2: glVertexAttribPointer(vao->ids[i], 3, GL_FLOAT, GL_FALSE, vao->stride, (void*)vao->offsets[i]); break;
			case 3: glVertexAttribPointer(vao->ids[i], 4, GL_FLOAT, GL_FALSE, vao->stride, (void*)vao->offsets[i]); break;
			case 4: glVertexAttribIPointer(vao->ids[i], 1, GL_INT, vao->stride, (void*)vao->offsets[i]); break;
			case 5: glVertexAttribIPointer(vao->ids[i], 2, GL_INT, vao->stride, (void*)vao->offsets[i]); break;
			case 6: glVertexAttribIPointer(vao->ids[i], 3, GL_INT, vao->stride, (void*)vao->offsets[i]); break;
			case 7: glVertexAttribIPointer(vao->ids[i], 4, GL_INT, vao->stride, (void*)vao->offsets[i]); break;
			case 8: glVertexAttribIPointer(vao->ids[i], 1, GL_UNSIGNED_INT, vao->stride, (void*)vao->offsets[i]); break;
			case 9: glVertexAttribIPointer(vao->ids[i], 2, GL_UNSIGNED_INT, vao->stride, (void*)vao->offsets[i]); break;
			case 10: glVertexAttribIPointer(vao->ids[i], 3, GL_UNSIGNED_INT, vao->stride, (void*)vao->offsets[i]); break;
			case 11: glVertexAttribIPointer(vao->ids[i], 4, GL_UNSIGNED_INT, vao->stride, (void*)vao->offsets[i]); break;
		}
	}
	vao->n_vaos++;
}

// records a command into a command buffer
void record_command(cbo_t* cmd_buffer, uint8_t opcode, void* info, uint32_t info_length) {
	cmd_buffer->cmds = realloc(cmd_buffer->cmds, cmd_buffer->size + info_length + 1);	// allocate memory for recording this command
	*(uint8_t*)(cmd_buffer->cmds+cmd_buffer->size) = opcode;
	memcpy(cmd_buffer->cmds+cmd_buffer->size+1, info, info_length);
	cmd_buffer->size += info_length + 1;
}

// returns 1 if layout_1 and layout_2 are identical, and 0 otherwise
uint8_t check_layouts_identical(set_layout_t* layout_1, set_layout_t* layout_2);

// given a CBO and pipeline, determine whether or not undefined behavior is triggered (based on the bound sets, and set layout for each accessible set binding)
// returns 1 if undefined behavior is triggered, and 0 otherwise
uint8_t check_undefined_behavior(cbo_t* cbo, pipeline_t* pipeline) {
	for(uint32_t i = 0; i < pipeline->n_desc_sets; i++) {
		if(objects[pipeline->dset_layout_ids[i]-1].deleted) return 1;	// undefined behavior: any set layout object bound for accessible set binding has been deleted
		if(cbo->dset_ids[i] == 0 || objects[cbo->dset_ids[i]-1].deleted) return 1;	// undefined behavior: any of the descriptor set bindings in the CBO does not exist/was deleted		
		if(objects[objects[cbo->dset_ids[i]-1].dset.layout_id-1].deleted) return 1;	// undefined behavior: any bound set's layout object (set_layout_t->layout_id) was deleted
		if(!check_layouts_identical(&objects[pipeline->dset_layout_ids[i]-1].set_layout, &objects[objects[cbo->dset_ids[i]-1].dset.layout_id-1].set_layout))
			return 1;
	}
	return 0;
}

void gl_set_pipeline_state(pipeline_t* pipeline) {
	GLenum attachments[8];
	for(uint32_t i = 1; i < 8; i++) attachments[i] = GL_NONE;
	for(uint32_t i = 0; i <= pipeline->n_enabled_attachments; i++) attachments[i] = GL_COLOR_ATTACHMENT0+i;
	glDrawBuffers(pipeline->n_enabled_attachments+1, attachments);
	glFrontFace(GL_CW);
	if(pipeline->culled_winding) glEnable(GL_CULL_FACE);
	switch(pipeline->culled_winding) {
		case 0: glDisable(GL_CULL_FACE); break;
		case 1: glCullFace(GL_FRONT); break;
		case 2: glCullFace(GL_BACK);  break;
		case 3: glCullFace(GL_FRONT_AND_BACK); break; 
	}
	if(pipeline->depth_enabled) glDepthMask(GL_TRUE);
	else glDepthMask(GL_FALSE);
	glEnable(GL_DEPTH_TEST);
	switch(pipeline->depth_pass) {
		case 0: glDepthFunc(GL_ALWAYS);  break; case 1: glDepthFunc(GL_NEVER);  break;
		case 2: glDepthFunc(GL_LESS);    break; case 3: glDepthFunc(GL_LEQUAL); break;
		case 4: glDepthFunc(GL_GREATER); break; case 5: glDepthFunc(GL_GEQUAL); break;
		case 6: glDepthFunc(GL_EQUAL);   break; case 7: glDepthFunc(GL_NOTEQUAL); break;
	}
	GLenum func;
	switch(pipeline->cw_stencil_pass) {
		case 0: func = GL_ALWAYS; break; case 1: func = GL_NEVER;   break; case 2: func = GL_LESS; break;
		case 3: func = GL_LEQUAL; break; case 4: func = GL_GREATER; break; case 5: func = GL_GEQUAL; break;
		case 6: func = GL_EQUAL;  break; case 7: func = GL_NOTEQUAL; break;
	}
	glStencilFuncSeparate(GL_FRONT, func, pipeline->cw_stencil_ref, pipeline->cw_stencil_func_mask);
	glStencilMaskSeparate(GL_FRONT, pipeline->cw_stencil_write_mask);
	switch(pipeline->ccw_stencil_pass) {
		case 0: func = GL_ALWAYS; break; case 1: func = GL_NEVER;   break; case 2: func = GL_LESS; break;
		case 3: func = GL_LEQUAL; break; case 4: func = GL_GREATER; break; case 5: func = GL_GEQUAL; break;
		case 6: func = GL_EQUAL;  break; case 7: func = GL_NOTEQUAL; break;
	}
	glStencilFuncSeparate(GL_BACK, func, pipeline->ccw_stencil_ref, pipeline->ccw_stencil_func_mask);
	glStencilMaskSeparate(GL_BACK, pipeline->ccw_stencil_write_mask);
	GLenum funcs[6];
	uint8_t vals[6] = { pipeline->cw_stencil_op_sfail, pipeline->cw_stencil_op_spass_dfail, pipeline->cw_stencil_op_sfail_dfail,
		pipeline->ccw_stencil_op_sfail, pipeline->ccw_stencil_op_spass_dfail, pipeline->ccw_stencil_op_sfail_dfail };
	for(uint32_t i = 0; i < 6; i++) switch(vals[i]) {
		case 0: funcs[i] = GL_KEEP;      break; case 1: funcs[i] = GL_ZERO;      break;
		case 2: funcs[i] = GL_REPLACE;   break; case 3: funcs[i] = GL_INCR;      break;
		case 4: funcs[i] = GL_DECR;      break; case 5: funcs[i] = GL_INCR_WRAP; break;
		case 6: funcs[i] = GL_DECR_WRAP; break; case 7: funcs[i] = GL_INVERT;    break;
	}
	glStencilOpSeparate(GL_FRONT, funcs[0], funcs[1], funcs[2]);
	glStencilOpSeparate(GL_BACK, funcs[3], funcs[4], funcs[5]);
	glColorMask(pipeline->color_write_mask & 0x8 ? GL_TRUE : GL_FALSE,
		pipeline->color_write_mask & 0x4 ? GL_TRUE : GL_FALSE,
		pipeline->color_write_mask & 0x2 ? GL_TRUE : GL_FALSE,
		pipeline->color_write_mask & 0x1 ? GL_TRUE : GL_FALSE);
	if(pipeline->src_color_blend_fac == 0 && pipeline->dst_color_blend_fac == 1
	&& pipeline->src_alpha_blend_fac == 0 && pipeline->dst_alpha_blend_fac == 1)
		glDisable(GL_BLEND); // src factor 1 and dst factor 0 = no blending
	else glEnable(GL_BLEND);
	vals[0] = pipeline->color_blend_op; vals[1] = pipeline->alpha_blend_op;
	for(uint32_t i = 0; i < 2; i++) switch(vals[i]) {
		case 0: funcs[i] = GL_FUNC_ADD; break;
		case 1: funcs[i] = GL_FUNC_SUBTRACT; break;
		case 2: funcs[i] = GL_FUNC_REVERSE_SUBTRACT; break;
		case 3: funcs[i] = GL_MIN; break;
		case 4: funcs[i] = GL_MAX; break;
	}
	glBlendEquationSeparate(funcs[0], funcs[1]);
	vals[0] = pipeline->src_color_blend_fac; vals[1] = pipeline->dst_color_blend_fac;
	vals[2] = pipeline->src_alpha_blend_fac; vals[3] = pipeline->dst_alpha_blend_fac;
	for(uint32_t i = 0; i < 4; i++) switch(vals[i]) {
		case 0: funcs[i] = GL_ONE; break;			case 1: funcs[i] = GL_ZERO; break;
		case 2: funcs[i] = GL_SRC_COLOR; break;		case 3: funcs[i] = GL_DST_COLOR; break;
		case 4: funcs[i] = GL_SRC_ALPHA; break;		case 5: funcs[i] = GL_DST_ALPHA; break;
		case 6: funcs[i] = GL_ONE_MINUS_SRC_COLOR; break;	case 7: funcs[i] = GL_ONE_MINUS_DST_COLOR; break;
		case 8: funcs[i] = GL_ONE_MINUS_SRC_ALPHA; break;	case 9: funcs[i] = GL_ONE_MINUS_DST_ALPHA; break;
	}
	glBlendFuncSeparate(funcs[0], funcs[1], funcs[2], funcs[3]);
}

void gl_set_uniform(GLint loc, uint8_t data_type, uint16_t elcount, GLvoid* data) {
	switch(data_type) {
		case 0: glUniform2fv(loc, elcount, (GLfloat*)data); break;
		case 1: glUniform3fv(loc, elcount, (GLfloat*)data); break;
		case 2: glUniform4fv(loc, elcount, (GLfloat*)data); break;
		case 3: glUniform2iv(loc, elcount, (GLint*)data); break;
		case 4: glUniform3iv(loc, elcount, (GLint*)data); break;
		case 5: glUniform4iv(loc, elcount, (GLint*)data); break;
		case 6: glUniform2uiv(loc, elcount, (GLuint*)data); break;
		case 7: glUniform3uiv(loc, elcount, (GLuint*)data); break;
		case 8: glUniform4uiv(loc, elcount, (GLuint*)data); break;
		case 9: glUniformMatrix2fv(loc, elcount, GL_FALSE, (GLfloat*)data); break;
		case 10: glUniformMatrix2x3fv(loc, elcount, GL_FALSE, (GLfloat*)data); break;
		case 11: glUniformMatrix2x4fv(loc, elcount, GL_FALSE, (GLfloat*)data); break;
		case 12: glUniformMatrix3x2fv(loc, elcount, GL_FALSE, (GLfloat*)data); break;
		case 13: glUniformMatrix3fv(loc, elcount, GL_FALSE, (GLfloat*)data); break;
		case 14: glUniformMatrix3x4fv(loc, elcount, GL_FALSE, (GLfloat*)data); break;
		case 15: glUniformMatrix4x2fv(loc, elcount, GL_FALSE, (GLfloat*)data); break;
		case 16: glUniformMatrix4x3fv(loc, elcount, GL_FALSE, (GLfloat*)data); break;
		case 17: glUniformMatrix4fv(loc, elcount, GL_FALSE, (GLfloat*)data); break;
		case 18: glUniform1fv(loc, elcount, (GLfloat*)data); break;
		case 19: glUniform1iv(loc, elcount, (GLint*)data); break;
		case 20: glUniform1uiv(loc, elcount, (GLuint*)data); break;
	}
}

void upload_push_constants(definition_t* defs, uint32_t n_defs, pipeline_t* pipeline) {
	if(!pipeline->n_push_constant_bytes) return;
	uint32_t offset = 0;
	for(uint32_t d = 0; d < n_defs; d++) {
		if(defs[d].def_type != UNIF_DEF_BIT || !defs[d].location_id) continue;	// not push constant
		// use glGetUniformLocation to get the location of the uniform (the GL program has already been set as in use)
		uint32_t id = defs[d].id;
		char* glsl_id = calloc(1,1);
		str_add(&glsl_id, "_");
		str_add_ui(&glsl_id, id);
		GLint loc = glGetUniformLocation(pipeline->gl_program, glsl_id);
		free(glsl_id);
		uint16_t elcount = defs[d].elcount;
		// upload data for uniform using glUniform* functions
		uint8_t type_size = 4;
		if(defs[d].data_type < 9)
			type_size *= VEC_SIZE(defs[d].data_type);
		else if(defs[d].data_type < 18)
			type_size *= MAT_SIZE(defs[d].data_type);
		if(loc >= 0) // OpenGL may remove uniforms that are unused; in that case, don't attempt to upload uniform data
			gl_set_uniform(loc, defs[d].data_type, elcount, pipeline->push_constant_data + offset);
		offset += type_size*elcount;
	}
}

// read all descriptors in the descriptor set to upload all uniform data (glUniform*) and sampler data (glActiveTexture, glBindTexture)
// there is also storage and acceleration structure descriptors, but they will not be handled here
// do this any time there's a new descriptor set bound, or for each accessible set bound when a new pipeline is bound
void upload_descriptor_set_data(cbo_t* cbo, desc_set_t* dset, uint32_t set_num, uint32_t* textures_occupied, pipeline_t* pipeline) {
	for(uint32_t i = 0; i < dset->n_bindings+1; i++) { // for each binding point in the set
		desc_binding_t* binding = &dset->bindings[i];	// current descriptor binding point
		for(uint32_t desc = 0; desc < binding->n_descs; desc++) { // for each descriptor in current binding point
			if(binding->object_ids[desc] == 0 || objects[binding->object_ids[desc]-1].deleted) continue; // if any descriptors are encountered that refer to non-existent or deleted objects, skip them and do nothing (undefined behavior)
			object_t* object = &objects[binding->object_ids[desc]-1]; // this is the object the descriptor refers to
			for(uint32_t loop = 0; loop < (pipeline->type != 2 ? 2 : 1); loop++) { // run twice if rasterization pipeline, once if compute pipeline
				definition_t* defs = (loop == 0) ? pipeline->defs_1 : pipeline->defs_2;
				uint32_t n_defs = (loop == 0) ? pipeline->n_defs_1 : pipeline->n_defs_2;
				uint32_t ubo_offset = 0; // this is offset into the UBO if uploading uniform data (if a read exceeds the end of the UBO, skip the rest)
				if(object->type == TYPE_TBO) {
					for(uint32_t d = 0; d < n_defs; d++) { // find shader definition with equal set + binding as this sampler descriptor
						int32_t id = -1;
						if(defs[d].data_type >= 21 && defs[d].set == set_num && defs[d].binding == binding->binding_number)
							id = defs[d].id;
						if(id < 0) continue; // sampler definition w/ equivalent set/binding not found, skip
						// use glGetUniformLocation to get the location of the uniform (the GL program has already been set as in use)
						char* glsl_id = calloc(1,1);
						str_add(&glsl_id, "_");
						str_add_ui(&glsl_id, id);
						str_add(&glsl_id, "[");
						str_add_ui(&glsl_id, desc);
						str_add(&glsl_id, "]");
						GLint loc = glGetUniformLocation(pipeline->gl_program, glsl_id);
						free(glsl_id);
						if(loc < 0) continue; // OpenGL may remove unused samplers

						// go through textures_occupied to find the first available texture unit (0)
						uint32_t unit;
						for(unit = 0; unit < max_number_samplers && textures_occupied[unit]; unit++);
						textures_occupied[unit] = set_num+1;
						glUniform1i(loc,textures_occupied[unit]-1);	// set the sampler's texture unit
						glActiveTexture(GL_TEXTURE0+textures_occupied[unit]-1);	// set the active texture unit
						glBindTexture(GL_TEXTURE_2D, object->tbo.gl_buffer);	// bind texture

						// set texture filtering modes (from sampler descriptor)
						switch(binding->min_filters[desc]) {
							case 0: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); break;
							case 1: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); break;
							case 2: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST); break;
							case 3: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST); break;
							case 4: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR); break;
							case 5: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); break;
						}
						switch(binding->mag_filters[desc]) {
							case 0: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); break;
							case 1: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); break;
						}
						switch(binding->s_modes[desc]) {
							case 0: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); break;
							case 1: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT); break;
							case 2: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); break;
						}
						switch(binding->t_modes[desc]) {
							case 0: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); break;
							case 1: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT); break;
							case 2: glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); break;
						}
					}
				} else if(object->type == TYPE_UBO) {
					for(uint32_t d = 0; d < n_defs; d++) {
						int32_t id = -1;
						if(defs[d].def_type == UNIF_DEF_BIT && !defs[d].location_id && defs[d].data_type < 21 && defs[d].set == set_num && defs[d].binding == binding->binding_number)
							id = defs[d].id;
						if(id < 0) continue; // uniform definition w/ equivalent set/binding not found, skip
						// use glGetUniformLocation to get the location of the uniform (the GL program has already been set as in use)
						char* glsl_id = calloc(1,1);
						str_add(&glsl_id, "_");
						str_add_ui(&glsl_id, id);
						GLint loc = glGetUniformLocation(pipeline->gl_program, glsl_id);
						free(glsl_id);
						uint16_t elcount = defs[d].elcount;
						// upload data for uniform using glUniform* functions
						uint8_t type_size = 4; // get the size of each uniform element
						if(defs[d].data_type < 9)		type_size *= VEC_SIZE(defs[d].data_type);
						else if(defs[d].data_type < 18)	type_size *= MAT_SIZE(defs[d].data_type);
						if(ubo_offset+elcount*type_size > object->ubo.size)
							break; // can't upload uniform; not enough data in UBO
						if(loc >= 0) // OpenGL may remove uniforms that are unused; in that case, don't attempt to upload uniform data
							gl_set_uniform(loc, defs[d].data_type, elcount, object->ubo.data+ubo_offset);
						ubo_offset += type_size*elcount;
					}
				}
			}
		}
	}
}

// executes all the commands in a command buffer
void submit_cmds(cbo_t* cbo) {
	if(cbo->pipeline_type == 2) return;	// cmd buffer never had a pipeline binding command recorded to it
	cbo->bindings[0] = 0;	// clear pipeline binding
	cbo->bindings[2] = 0;	// clear VBO binding
	cbo->bindings[3] = 0;	// clear IBO binding
	for(uint32_t i = 0; i < MAX_NUMBER_BOUND_SETS; i++)
		cbo->dset_ids[i] = 0;	// clear all descriptor set bindings

	// bind the FBO used by the CBO
	fbo_t* fbo;
	if(cbo->pipeline_type == 0) {	// the bound FBO only matters for rasterization pipelines
		if(cbo->bindings[1] != 0) {
			object_t* fbo_object = &objects[cbo->bindings[1]-1];
			if(fbo_object->deleted) return;	// the FBO bound to the command buffer being submitted has previously been deleted
			fbo = &fbo_object->fbo;
			if(fbo->width == 0 || fbo->height == 0) return;	// there are no attachments for this FBO
			glBindFramebuffer(GL_FRAMEBUFFER, fbo->gl_buffer);
		}
		else glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	// RESETTING THE COMMAND BUFFER WILL CLEAR THE PIPELINE TYPE

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	pipeline_t* pipeline = 0;	// the currently bound pipeline
	uint8_t* cmds = cbo->cmds;
	uint8_t undefined_behavior = 0; // whether or not there is undefined behavior based on current set binding layouts for bound pipeline + currently bound sets
	uint32_t textures_occupied[max_number_samplers]; // for the current pipeline, records the set binding for each sampler bound to a texture unit
	vao_t* current_vao = 0;
	while(cmds < (uint8_t*)cbo->cmds+cbo->size) {
		uint64_t id, is_indexed, n_indices, n_instances, start_idx, offset, n_bytes;
		uint8_t set, attachments;
		GLenum p_type;
		switch(*cmds) {	// opcode
			case 77:	// bind a pipeline
				cbo->bindings[0] = *(uint64_t*)(cmds+1);	// set, for the CBO, the bound pipeline ID
				cmds += 9;
				for(uint32_t i = 0; i < max_number_samplers; i++) textures_occupied[i] = 0;
				// bindings are bound object IDs for the command buffer: bindings[0] = pipeline object, bindings[1] = FBO, bindings[2] = VBO, bindings[3] = IBO
				object_t* pipeline_object = &objects[cbo->bindings[0]-1];
				if(pipeline_object->deleted) break;	// the pipeline bound to the command buffer being submitted has previously been deleted
				pipeline = &pipeline_object->pipeline;	// known to not be a ray tracing pipeline; ray tracing pipeline binds are not recorded
				undefined_behavior = check_undefined_behavior(cbo, pipeline);
				object_t* vao_object = &objects[pipeline->vao_id-1];
				if(vao_object->deleted) return;
				current_vao = &vao_object->vao;
				glUseProgram(pipeline->gl_program);
				// upload all descriptor set data for all accessible descriptor sets
				for(uint32_t i = 0; i < pipeline->n_desc_sets; i++) {
					if(cbo->dset_ids[i] == 0 || objects[cbo->dset_ids[i]-1].deleted) continue; // do not account for descriptor sets which have not been bound
					upload_descriptor_set_data(cbo, &objects[cbo->dset_ids[i]-1].dset, i, textures_occupied, pipeline);
				}
				switch(pipeline->primitive_type) {
					case 0: p_type = GL_TRIANGLES; break;
					case 1: p_type = GL_LINES; break;
					case 2: p_type = GL_POINTS; break;
				}
				// reset/init the pipeline's push constant data
				if(pipeline->push_constant_data) {
					free(pipeline->push_constant_data);
					pipeline->push_constant_data = 0;
				}
				if(pipeline->n_push_constant_bytes)	pipeline->push_constant_data = calloc(1,pipeline->n_push_constant_bytes);
				upload_push_constants(pipeline->defs_1, pipeline->n_defs_1, pipeline);
				upload_push_constants(pipeline->defs_2, pipeline->n_defs_2, pipeline);
				gl_set_pipeline_state(pipeline);
				break;
			case 79:	// bind a descriptor set to a set in the bound pipeline, or VBO/IBO within the bound command buffer
				id = *(uint64_t*)(cmds+1);
				set = *(cmds+9);
				cmds += 10;
				object_t* object = &objects[id-1];
				if(object->deleted) break;	// the descriptor set/VBO/IBO bound to the command buffer being submitted has previously been deleted
				if(object->type == TYPE_VBO) {	// VBO bind
					cbo->bindings[2] = id;
					bind_vbo(current_vao, id);
				} else if(object->type == TYPE_IBO) {	// IBO bind
					cbo->bindings[3] = id;
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, object->gl_buffer);
				} else if(object->type == TYPE_DSET) {	// descriptor set bind
					for(uint32_t i = 0; i < max_number_samplers; i++)
						if(textures_occupied[i] == set+1) {
							textures_occupied[i] = 0;
							glActiveTexture(GL_TEXTURE0+i);		// set the active texture unit
							glBindTexture(GL_TEXTURE_2D, 0);	// unbind texture from texture unit
						}

					// binds the descriptor set 'object' in cbo to 'set'
					cbo->dset_ids[set] = id;
					undefined_behavior = check_undefined_behavior(cbo, pipeline); // update whether or not behavior is undefined (based on bound sets + pipeline set layouts)
					upload_descriptor_set_data(cbo, &object->dset, set, textures_occupied, pipeline);
				}
				break;
			case 86:	// clear buffers
				attachments = cmds[1];
				if(attachments < 9) {
					float r = *(float*)(cmds+2),g = *(float*)(cmds+6),b = *(float*)(cmds+10),a = *(float*)(cmds+14);
					glClearColor(r,g,b,a);
					if(cbo->bindings[1] == 0 && attachments < 2) glClear(GL_COLOR_BUFFER_BIT);
					else if(cbo->bindings[1] != 0) {
						GLenum drawbuffs[] = { GL_DRAW_BUFFER0,GL_DRAW_BUFFER1,GL_DRAW_BUFFER2,GL_DRAW_BUFFER3,GL_DRAW_BUFFER4,GL_DRAW_BUFFER5,GL_DRAW_BUFFER6,GL_DRAW_BUFFER7};
						if(!attachments)
							for(uint32_t i = 0; i <= pipeline->n_enabled_attachments; i++) glClearBufferfv(GL_COLOR, drawbuffs[i], (GLfloat*)(cmds+2));
						else glClearBufferfv(GL_COLOR, GL_DRAW_BUFFER0+attachments, (GLfloat*)(cmds+2));
					}
					cmds += 18;
				} else if(attachments == 9) {
					float depth = *(float*)(cmds+2);
					glClearDepth(depth);
					glClear(GL_DEPTH_BUFFER_BIT);
					cmds += 6;
				} else {
					uint8_t stencil = cmds[2];
					glClearStencil(stencil);
					glClear(GL_STENCIL_BUFFER_BIT);
					cmds += 3;
				}
				break;
			case 92:	// direct draw call
				// is_indexed, n_indices, first_index, n_instances
				// the width of this is 4*4 (16)
				is_indexed = *(uint32_t*)(cmds+1);
				n_indices = *(uint32_t*)(cmds+5);
				start_idx = *(uint32_t*)(cmds+9);
				n_instances = *(uint32_t*)(cmds+13) + 1;
				cmds += 17; // go to next opcode
				if(undefined_behavior) break;
				if(!cbo->bindings[2]) break;	// if no VBO bound
				if(!is_indexed && n_instances == 1)
					glDrawArrays(p_type, start_idx, n_indices);
				else if(is_indexed && n_instances == 1) 
					glDrawElements(p_type, n_indices, GL_UNSIGNED_INT, (GLvoid*)(start_idx*4));
				else if(!is_indexed)
					glDrawArraysInstanced(p_type, start_idx, n_indices, n_instances);
				else glDrawElementsInstanced(p_type, n_indices, GL_UNSIGNED_INT, (GLvoid*)(start_idx*4), n_instances);
				break;
			case 93:	// indirect draw call
				is_indexed = *(uint64_t*)(cmds+1);
				id = *(uint64_t*)(cmds+2);
				offset = *(uint64_t*)(cmds+10);
				uint64_t n_draws = *(uint64_t*)(cmds+18) + 1;
				cmds += 33; // go to next opcode
				if(objects[id-1].deleted) break; // break if the data buffer has been deleted
				if(n_draws * 12 + offset > objects[id-1].dbo.size) break; // draw calls exceed size of buffer
				uint32_t* params = (uint32_t*)(objects[id-1].dbo.data + offset);
				for(uint32_t i = 0; i < n_draws; i++) {
					n_indices = params[0];
					n_instances = params[1]+1;
					start_idx = params[2];
					// indexed/non-indexed depends on is_indexed
					if(!is_indexed && n_instances == 1) glDrawArrays(p_type, start_idx, n_indices);
					else if(is_indexed && n_instances == 1) glDrawElements(p_type, n_indices, GL_UNSIGNED_INT, (GLvoid*)(start_idx*4));
					else if(!is_indexed) glDrawArraysInstanced(p_type, start_idx, n_indices, n_instances);
					else glDrawElementsInstanced(p_type, n_indices, GL_UNSIGNED_INT, (GLvoid*)(start_idx*4), n_instances);
					params += 3;
				}
				break;
			case 94:	// data buffer update
				id = *(uint64_t*)(cmds+1);
				offset = *(uint64_t*)(cmds+9);
				n_bytes = *(uint64_t*)(cmds+17) + 1;
				cmds += 25 + n_bytes;
				if(objects[id-1].deleted) break;
				if(offset + n_bytes > objects[id-1].dbo.size) break;
				if(!objects[id-1].dbo.data) break;
				memcpy(objects[id-1].dbo.data, cmds+25, n_bytes);
				break;
			case 95:	// update push constants
				id = *(uint64_t*)(cmds+1);
				offset = *(uint64_t*)(cmds+9);
				n_bytes = *(uint64_t*)(cmds+17) + 1;
				cmds += 25; // go to next opcode
				if(objects[id-1].deleted) break;
				if(offset + n_bytes > objects[id-1].dbo.size) break;
				if(n_bytes > pipeline->n_push_constant_bytes) break;
				if(!objects[id-1].dbo.data) break;
				memcpy(pipeline->push_constant_data, objects[id-1].dbo.data+offset, n_bytes);
				upload_push_constants(pipeline->defs_1, pipeline->n_defs_1, pipeline);
				upload_push_constants(pipeline->defs_2, pipeline->n_defs_2, pipeline);
				break;
		}
	}
}

// returns 1 if layout_1 and layout_2 are identical, and 0 otherwise
uint8_t check_layouts_identical(set_layout_t* layout_1, set_layout_t* layout_2) {
	if(layout_1->n_binding_points != layout_2->n_binding_points)
		return 0;
	uint32_t n_identical_bindings = 0;
	// find each where binding number of layout 1 (index j) is in layout 2 (index k), and then check if the bindings are identical
	for(uint32_t j = 0; j < layout_1->n_binding_points+1; j++)
		for(uint32_t k = 0; k < layout_2->n_binding_points+1; k++) {
			if(layout_1->binding_numbers[j] != layout_2->binding_numbers[k]) continue;
			if(layout_1->binding_types[j] == layout_2->binding_types[k]
			&& layout_1->n_descs[j] == layout_2->n_descs[k])
				n_identical_bindings++;
		}
	if(n_identical_bindings == layout_1->n_binding_points+1)
		return 1;
	return 0;
}

// creates a descriptor set layout from provided information (allocates and fills the data in 'layout').
// no memory bound checking required; all checking done before call in instruction_72 
void create_set_layout(set_layout_t* layout, uint8_t* info, uint64_t thread_id) {
	uint32_t n_binding_points = *(uint32_t*)info;
	uint32_t binding_numbers[n_binding_points+1];
	uint8_t binding_types[n_binding_points+1];
	uint16_t desc_counts[n_binding_points+1];
	info += 4;
	for(uint32_t i = 0; i < n_binding_points+1; i++) {
		binding_numbers[i] = ((uint32_t*)info)[0];
		binding_types[i] = *(info+4);
		desc_counts[i] = 1;
		if(binding_types[i] == 2) { desc_counts[i] = *(uint16_t*)(info+5); info += 7; }
		else info += 5;
	}

	// create the new descriptor set layout
	layout->binding_numbers = malloc(sizeof(binding_numbers));
	memcpy(layout->binding_numbers, binding_numbers, sizeof(binding_numbers));
	layout->binding_types = malloc(sizeof(binding_types));
	memcpy(layout->binding_types, binding_types, sizeof(binding_types));
	layout->n_descs = malloc(sizeof(desc_counts));
	memcpy(layout->n_descs, desc_counts, sizeof(desc_counts));
	layout->n_binding_points = n_binding_points;
}

// creates a VAO given the VAO creation info (allocate and fill data in 'vao')
// no memory bound checking required; all checking done before call in instruction_72 
void create_vao(vao_t* vao, uint8_t* info, uint8_t* success) {
	*success = 0;	
	vao->n_attribs = (*(uint16_t*)info)+1;
	vao->stride = *(uint64_t*)(info+2);
	if(!vao->stride) return;
	if(vao->stride % 4) return;		// stride must be a multiple of 4
	info += 10;
	vao->ids = malloc(sizeof(uint16_t)*vao->n_attribs);
	vao->offsets = malloc(sizeof(uint64_t)*vao->n_attribs);
	vao->formats = malloc(sizeof(uint8_t)*vao->n_attribs);
	for(uint32_t i = 0; i < vao->n_attribs; i++) {
		vao->ids[i] = *(uint16_t*)(&info[i*11]);
		for(uint32_t j = 0; j < i; j++)
			if(vao->ids[j] == vao->ids[i]) return; // id was repeated
		vao->offsets[i] = *(uint64_t*)(&info[i*11+2]);
		vao->formats[i] = info[i*11+10];
		if(vao->offsets[i] % 4) return;		// offset must be a multiple of 4
		if(vao->formats[i] > 11) return;	// invalid vertex attribute format given
		uint32_t attrib_size = ((vao->formats[i]%4)+1)*4;
		if(vao->offsets[i]+attrib_size > vao->stride) return; // attribute offset + data size > stride
	}
	*success = 1;
}

// creates a pipeline given the pipeline creation info (allocate and fill data in 'pipeline')
// 'success' will be set 0 if the pipeline creation fails, and 1 otherwise
// no memory bound checking required; all checking done before call in instruction_72 
void create_pipeline(pipeline_t* pipeline, uint8_t* info, uint8_t* success, thread_t* thread) {
	// EACH PIPELINE WILL GET ITS OWN SHADER PROGRAM, EVEN IF ONE WITH IDENTICAL SHADERS ALREADY EXISTS, TO SIMPLIFY THE VM/
	// SET ALL PIPELINE STATE HERE.
	// BUILD_SHADER GENERATED GLSL SOURCE WILL EXIST ONLY IN THIS FUNCTION TO CREATE SHADER PROGRAMS AND IS NOT STORED IN SHADER OBJECTS, PIPELINE OBJECTS, OR ANYWHERE ELSE.

	*success = 0;
	if(pipeline->type == 0) {	// if creating a rasterization pipeline
		//
		// GET INFORMATION ABOUT THE PIPELINE'S SHADERS AND VAO TO BE CONFIGURED LATER
		//

		uint64_t vshader_id = ((uint64_t*)info)[0];
		if(vshader_id == 0 || vshader_id > n_objects) return;
		object_t* vshader_object = &objects[vshader_id-1];
		if(vshader_object->deleted) return;
		if(vshader_object->privacy_key != thread->privacy_key) return;
		if(vshader_object->type != TYPE_VSH) return;
		uint64_t pshader_id = ((uint64_t*)info)[1];
		if(pshader_id == 0 || pshader_id > n_objects) return;
		object_t* pshader_object = &objects[pshader_id-1];
		if(pshader_object->deleted) return;
		if(pshader_object->privacy_key != thread->privacy_key) return;
		if(pshader_object->type != TYPE_PSH) return;
		uint64_t vao_id = ((uint64_t*)info)[2];
		if(vao_id == 0 || vao_id > n_objects) return;
		object_t* vao_object = &objects[vao_id-1];
		if(vao_object->deleted) return;
		if(vao_object->privacy_key != thread->privacy_key) return;
		if(vao_object->type != TYPE_VAO) return;

		pipeline->vao_id = vao_id;

		//
		// SET THE RASTERIZATION STATE
		//

		pipeline->culled_winding = info[24];
		if(pipeline->culled_winding > 3) return;
		pipeline->primitive_type = info[25];
		if(pipeline->primitive_type > 2) return;
		pipeline->n_push_constant_bytes = info[26];
		if(pipeline->n_push_constant_bytes % 4 != 0 || pipeline->n_push_constant_bytes > 128) return;
		pipeline->n_desc_sets = *(uint16_t*)(info+27);
		if(pipeline->n_desc_sets > MAX_NUMBER_BOUND_SETS) return;
		info = &info[29];	// info is now pointer to first byte after accessible set count
		// for each accessible descriptor set layout
		uint32_t n_ubos = 0;		// number of UBOs in accessible set layouts
		uint32_t n_samplers = 0;	// number of samplers in accessible set layouts
		for(uint32_t i = 0; i < pipeline->n_desc_sets; i++) {
			uint64_t layout_id = ((uint64_t*)(info))[i];
			if(layout_id == 0 || layout_id > n_objects) return;
			object_t* object = &objects[layout_id-1];
			if(object->deleted) return;
			if(object->privacy_key != thread->privacy_key) return;
			if(object->type != TYPE_SET_LAYOUT) return;
			// make sure there's not too many UBOS + sampler descriptors, <= 1 sampler descriptor per binding, and no AS, SBO, or images in the accessible set layout
			set_layout_t* layout = &object->set_layout;
			for(uint32_t j = 0; j < layout->n_binding_points+1; j++) {
				if(layout->binding_types[j] == 1 || layout->binding_types[j] > 2) return; // if not uniform or sampler descriptor, pipeline creation fails
				if(layout->binding_types[j] == 0) n_ubos++;
				if(layout->binding_types[j] == 2) n_samplers += layout->n_descs[j];
			}
			info += 8;
			pipeline->dset_layout_ids[i] = layout_id;
		}
		if(n_ubos > max_number_ubos || n_samplers > max_number_samplers)
			return;
		// info is now pointer to byte after the descriptor set layout IDs
		pipeline->depth_pass = *info;
		if(pipeline->depth_pass > 7) return;
		if(info[1] > 1) return;
		pipeline->depth_enabled = !info[1];
		pipeline->cw_stencil_ref = info[2];
		pipeline->cw_stencil_pass = info[3];
		if(pipeline->cw_stencil_pass > 7) return;
		pipeline->cw_stencil_op_sfail = info[4];
		if(pipeline->cw_stencil_op_sfail > 7) return;
		pipeline->cw_stencil_op_spass_dfail = info[5];
		if(pipeline->cw_stencil_op_spass_dfail > 7) return;
		pipeline->cw_stencil_op_sfail_dfail = info[6];
		if(pipeline->cw_stencil_op_sfail_dfail > 7) return;
		pipeline->cw_stencil_func_mask = info[7];
		pipeline->cw_stencil_write_mask = info[8];
		pipeline->ccw_stencil_ref = info[9];
		pipeline->ccw_stencil_pass = info[10];
		if(pipeline->ccw_stencil_pass > 7) return;
		pipeline->ccw_stencil_op_sfail = info[11];
		if(pipeline->ccw_stencil_op_sfail > 7) return;
		pipeline->ccw_stencil_op_spass_dfail = info[12];
		if(pipeline->ccw_stencil_op_spass_dfail > 7) return;
		pipeline->ccw_stencil_op_sfail_dfail = info[13];
		if(pipeline->ccw_stencil_op_sfail_dfail > 7) return;
		pipeline->ccw_stencil_func_mask = info[14];
		pipeline->ccw_stencil_write_mask = info[15];
		pipeline->color_write_mask = info[16]&0xF;
		pipeline->n_enabled_attachments = info[17]&0x7;
		if(pipeline->n_enabled_attachments > 7) return;
		pipeline->color_blend_op = info[18];
		if(pipeline->color_blend_op > 4) return;
		pipeline->src_color_blend_fac = info[19];
		if(pipeline->src_color_blend_fac > 9) return;
		pipeline->dst_color_blend_fac = info[20];
		if(pipeline->dst_color_blend_fac > 9) return;
		pipeline->alpha_blend_op = info[21];
		if(pipeline->alpha_blend_op > 4) return;
		pipeline->src_alpha_blend_fac = info[22];
		if(pipeline->src_alpha_blend_fac > 9) return;
		pipeline->dst_alpha_blend_fac = info[23];
		if(pipeline->dst_alpha_blend_fac > 9) return;

		//
		// BUILD THE PIPELINE'S SHADERS
		//

		shader_t glsl_vshader, glsl_pshader;
		glsl_vshader.src = calloc(1,1), glsl_vshader.size = 1, glsl_vshader.type = vshader_object->shader.type;
		glsl_pshader.src = calloc(1,1), glsl_pshader.size = 1, glsl_pshader.type = pshader_object->shader.type;

		shader_data_t vshader_data, pshader_data;
		memset(&vshader_data,0,sizeof(shader_data_t));
		memset(&pshader_data,0,sizeof(shader_data_t));

		/*** VALIDATE + TRANSLATE SOURCE CODE OF SHADER; RETURN IF INVALID ***/
		if(build_shader(vshader_object->shader.src, vshader_object->shader.size, vshader_object->shader.type, &glsl_vshader, &vshader_data) == 1) return;
		// copy over information important for pixel shader translation; need to know about the vertex shader's output IDs + their interpolation modes
		pshader_data.vertex_output_ids = vshader_data.vertex_output_ids;
		pshader_data.vertex_output_modes = vshader_data.vertex_output_modes;
		pshader_data.n_vertex_outputs = vshader_data.n_vertex_outputs;
		if(SHOW_SHADERS)
			printf("GLSL vertex shader: \n%s\n", glsl_vshader.src);

		if(build_shader(pshader_object->shader.src, pshader_object->shader.size, pshader_object->shader.type, &glsl_pshader, &pshader_data) == 1) return;
		pipeline->defs_1 = vshader_data.defs; pipeline->n_defs_1 = vshader_data.n_defs;
		pipeline->defs_2 = pshader_data.defs; pipeline->n_defs_2 = pshader_data.n_defs;
		if(SHOW_SHADERS)
			printf("GLSL pixel shader: \n%s\n", glsl_pshader.src);

		// push constant data checking
		if(vshader_data.n_push_constant_bytes && vshader_data.n_push_constant_bytes != pipeline->n_push_constant_bytes) return; // pipeline creation fails if a shader does not have same number of push constant bytes as pipeline
		if(pshader_data.n_push_constant_bytes && pshader_data.n_push_constant_bytes != pipeline->n_push_constant_bytes) return; // pipeline creation fails if a shader does not have same number of push constant bytes as pipeline
		if(vshader_data.n_push_constant_bytes && pshader_data.n_push_constant_bytes) {
			int32_t first_push_i = -1, first_push_j = -1, n_push_i = 0, n_push_j = 0; // first push constant def indices for vshader, pshader
			for(uint32_t i = 0; i < vshader_data.n_defs; i++)
				if(vshader_data.defs[i].def_type == UNIF_DEF_BIT && vshader_data.defs[i].location_id)
					if(!(n_push_i++)) first_push_i = i;
			for(uint32_t j = 0; j < pshader_data.n_defs; j++)
				if(pshader_data.defs[j].def_type == UNIF_DEF_BIT && pshader_data.defs[j].location_id)
					if(!(n_push_j++)) first_push_j = j;
			// make sure the push constant block for each shader is the same layout; each have the same element count, type
			if(n_push_i != n_push_j) return;
			for(uint32_t i = 0; i < n_push_i && vshader_data.defs[first_push_i+i].location_id; i++) {
				if(vshader_data.defs[first_push_i+i].data_type != pshader_data.defs[first_push_j+i].data_type) return;
				if(vshader_data.defs[first_push_i+i].elcount != pshader_data.defs[first_push_j+i].elcount) return;
			}
		}

		// make sure each vertex shader output has a pixel shader input w/ same ID
		for(uint32_t i = 0; i < vshader_data.n_vertex_outputs; i++) {
			uint8_t found_matching = 0;
			for(uint32_t j = 0; j < pshader_data.n_pixel_inputs; j++)
				if(vshader_data.vertex_output_ids[i] == pshader_data.pixel_input_ids[j]) {
					if(vshader_data.vertex_output_types[i] != pshader_data.pixel_input_types[j]) return; // pipeline creation fails if any output/input pairs are not of same data type
					found_matching = 1;
					break;
				}
			if(!found_matching) return; // pipeline creation fails if any vertex shader output IDs are not found in pixel shader input IDs
		}

		// for all definitions, ensure it's not a sampler at non-sampler set/binding, not uniform data at a non-data uniform 
		// set/binding, not an accel struct at accel set/binding, not a storage at non-storage set/binding
		// also checks that a sampler's elcount is equal to the number of descriptors at the descriptor binding point
		for(uint32_t i = 0; i < pipeline->n_defs_1+pipeline->n_defs_2; i++) {
			uint32_t def_idx = i - (i < pipeline->n_defs_1 ? 0 : pipeline->n_defs_1);
			definition_t* def = i < pipeline->n_defs_1 ? &pipeline->defs_1[def_idx] : &pipeline->defs_2[def_idx];
			if((def->def_type == UNIF_DEF_BIT && !def->location_id) // uniform (but not push constant)
			|| (def->def_type == VAR_DEF_BIT  && def->within_block)) { // storage variable
				if(def->set > pipeline->n_desc_sets-1) return; // set binding inaccessible, pipeline creation fails
			} else continue; // not a uniform or storage variable definition occupying some descriptor binding
			set_layout_t set_layout = objects[pipeline->dset_layout_ids[def->set]-1].set_layout;
			int32_t binding_type = -1;
			uint16_t n_descs = 1;
			for(uint32_t j = 0; j < set_layout.n_binding_points+1; j++)
				if(set_layout.binding_numbers[j] == def->binding) { binding_type = set_layout.binding_types[j]; n_descs = set_layout.n_descs[j]; break; }
			if(binding_type == -1) return; // descriptor binding point for definition does not exist in set layout
			// now check that this def has a type compatible with binding_type
			if(def->def_type == VAR_DEF_BIT && binding_type != 1) return; // invalid; storage variable at non-storage descriptor
			else if(def->data_type < 21  && binding_type != 0) return; // invalid; uniform data at non-uniform descriptor
			else if(def->data_type > 20 && def->data_type < 24 && binding_type != 2) return; // invalid; sampler at non-sampler descriptor
			else if(def->data_type > 20 && def->data_type < 24 && def->elcount != 0 && def->elcount != n_descs) return; // sampler def incompatible with desc bind point
			else if(def->data_type == 24 && binding_type != 3) return; // invalid; image at non-image descriptor
			else if(def->data_type == 25 && binding_type != 4) return; // invalid; AS at non-AS descriptor
		}

		/*** COMPILE THE GENERATED GLSL SOURCE CODE INTO A NEW GL SHADER OBJECT ***/
		uint32_t gl_vshader_id = 0, gl_pshader_id = 0;
		pipeline->gl_program = glCreateProgram();
		gl_vshader_id = glCreateShader(GL_VERTEX_SHADER);
		gl_pshader_id = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(gl_vshader_id, 1, (const GLchar const**)&glsl_vshader.src, 0);
		glShaderSource(gl_pshader_id, 1, (const GLchar const**)&glsl_pshader.src, 0);
		glCompileShader(gl_vshader_id);
		GLint gl_success = 0;
		glGetShaderiv(gl_vshader_id, GL_COMPILE_STATUS, &gl_success);
		if(gl_success == GL_FALSE) {
			GLint log_size = 0;
			glGetShaderiv(gl_vshader_id, GL_INFO_LOG_LENGTH, &log_size);
			uint8_t* log = malloc(log_size);
			glGetShaderInfoLog(gl_vshader_id, log_size, 0, log);
			printf("create_pipeline() call failed to compile vertex shader, GL error: %s\n", log);
			glDeleteShader(gl_vshader_id);
			return;
		}		
		glCompileShader(gl_pshader_id);
		gl_success = 0;
		glGetShaderiv(gl_pshader_id, GL_COMPILE_STATUS, &gl_success);
		if(gl_success == GL_FALSE) {
			GLint log_size = 0;
			glGetShaderiv(gl_pshader_id, GL_INFO_LOG_LENGTH, &log_size);
			uint8_t* log = malloc(log_size);
			glGetShaderInfoLog(gl_pshader_id, log_size, 0, log);
			printf("create_pipeline() call failed to compile pixel shader, GL error: %s\n", log);
			glDeleteShader(gl_pshader_id);
			return;
		}
		glAttachShader(pipeline->gl_program, gl_vshader_id);
		glAttachShader(pipeline->gl_program, gl_pshader_id);
		glLinkProgram(pipeline->gl_program);
		gl_success = 0;
		glGetProgramiv(pipeline->gl_program, GL_LINK_STATUS, &gl_success);
		if(gl_success == GL_FALSE) {
			GLint log_size = 0;
			glGetProgramiv(pipeline->gl_program, GL_INFO_LOG_LENGTH, &log_size);
			uint8_t* log = malloc(log_size);
			glGetProgramInfoLog(pipeline->gl_program, log_size, 0, log);
			printf("create_pipeline() call failed to link shader program, GL error: %s\n", log);
			glDeleteProgram(pipeline->gl_program);
			return;
		}
	}
	if(pipeline->type == 1) return;	// this VM does not support ray tracing pipelines
	if(pipeline->type == 2) {	// if creating a compute pipeline
		uint64_t cshader_id = ((uint64_t*)info)[0];
		if(cshader_id == 0 || cshader_id > n_objects) return;
		object_t* cshader_object = &objects[cshader_id-1];
		if(cshader_object->deleted) return;
		if(cshader_object->privacy_key != thread->privacy_key) return;
		if(cshader_object->type != TYPE_CSH) return;
		pipeline->n_push_constant_bytes = info[8];
		if(pipeline->n_push_constant_bytes % 4 != 0 || pipeline->n_push_constant_bytes > 128) return;
		pipeline->n_desc_sets = *(uint16_t*)(info+9);
		if(pipeline->n_desc_sets > MAX_NUMBER_BOUND_SETS) return;
		info = &info[11];	// info is now pointer to first byte after accessible set count
		// for each accessible descriptor set layout
		uint32_t n_ubos = 0;		// number of UBOs in accessible set layouts
		uint32_t n_sbos = 0;		// number of SBOs in accessible set layouts
		uint32_t n_samplers = 0;	// number of samplers in accessible set layouts
		uint32_t n_images = 0;		// number of images in accessible set layouts
		for(uint32_t i = 0; i < pipeline->n_desc_sets; i++) {
			uint64_t layout_id = ((uint64_t*)info)[i];
			if(layout_id == 0 || layout_id > n_objects) return;
			object_t* object = &objects[layout_id-1];
			if(object->deleted) return;
			if(object->privacy_key != thread->privacy_key) return;
			if(object->type != TYPE_SET_LAYOUT) return;
			// make sure there's not too many sampler, UBO, or SBO descriptors, and no AS in the accessible set layout
			set_layout_t* layout = &object->set_layout;
			for(uint32_t j = 0; j < layout->n_binding_points+1; j++) {
				if(layout->binding_types[j] == 4) return; // if AS descriptor, pipeline creation fails
				if(layout->binding_types[j] == 0) n_ubos++;
				if(layout->binding_types[j] == 1) n_sbos++;
				if(layout->binding_types[j] == 2) n_samplers += layout->n_descs[j];
				if(layout->binding_types[j] == 3) n_images++;
			}
			info += 8;
			pipeline->dset_layout_ids[i] = layout_id;
		}
		if(n_ubos > max_number_ubos || n_sbos > max_number_sbos || n_samplers > max_number_samplers || n_images > max_number_images)
			return;
	}
	*success = 1;	// if the function reached here, there was no error in creating the pipeline object
}

// reads a string from main memory; returns 0 on error (segfault/non-terminated string) and sets segfault bit on segfault
uint32_t get_string_main_mem(thread_t* thread, uint64_t address, uint8_t** str) {
	if(!str) return 0;
	if(check_segfault(thread, address, 1)) return 0;

	uint32_t size = 1; // string length includes null character
	uint8_t terminated = 0;
	while(!check_segfault(thread, address, size) && size <= 1000) {
		char c = read_main_mem_val(thread, address+size-1, 1);
		if(c == '\0') {
			terminated = 1; // \0 found before segfault!
			break;
		}
		size++;
	}
	if(terminated || size == 1001) thread->regs[13] &= (~SR_BIT_SEGFAULT); // no segfault!
	if(size == 1001) return 0; // string did not terminate before 1001st character
	if(!terminated) return 0; // no null character found before segfault

	*str = read_main_mem(thread, address, size);

	return size;
}

// returns address to the file extension part of a file path string - 0 on error
uint8_t* get_string_file_ext(uint8_t* path_str) {
	char* pos = strrchr(path_str, '.');
	if(!pos) return 0;
	if(pos[1] == '\0') return 0;
	return pos + 1;
}

// concatenate a provided path with the implementation's root path (assumes path string is valid)
void get_full_path(uint8_t* path, uint8_t** output, uint32_t* output_size) {
	// output is allocated, output_size is number of bytes allocated
	// concatenate with root_path
	uint32_t size = strlen(path)+1;
	if(strlen(root_path) == 1 && (path[0] == '/' || path[0] == '\0')) {
		*output = calloc(1,2);	// special case: root path is /, path is / or \0
		**output = '/';
	} else if(path[0]=='/') {
		*output = (uint8_t*)malloc(strlen(root_path)+size);
		memcpy(*output+strlen(root_path), path, size);
	} else {
		*output = (uint8_t*)malloc(strlen(root_path)+size+1);
		(*output)[strlen(root_path)] = '/';
		memcpy(*output+strlen(root_path)+1, path, size);
	}
	memcpy(*output, root_path, strlen(root_path));
	*output_size = strlen(*output)+1;
}

// check the validity of a file path string
uint8_t validate_path(uint8_t* path) {
	uint8_t value = 0;
	uint8_t backslash = 0;	// used to check if there's any backslashes beside each other
	int32_t n_dots = 0;	// used to count consecutive . in each filename (. and .. are banned)
	for(uint32_t i = 0; i < 1000; i++) {
		uint8_t c = path[i];
		if(c == '\0') { if(n_dots != 1 && n_dots != 2) value = 1; break; }
		if(n_dots == 0 && c != '.') n_dots = -1;
		if(n_dots != -1 && c == '.') n_dots++;
		if(c == '/') {
			if(backslash) break; // found / next to each other
			backslash = 1;
			if(n_dots == 1 || n_dots == 2) break; // invalid file name; . or ..
			n_dots = 0;
			continue;
		}
		// if any \, :, *, ?, “, <, >, |, the path is invalid
		if(c=='\\'||c==':'||c=='*'||c=='?'||c=='\"'||c=='<'||c=='>'||c=='|') break;
		backslash = 0;
	}
	// return 0: invalid file path string
	// return 1: valid file path string
	return value;
}

// check if there's a file or directory with the specified path
uint8_t check_path_existence(uint8_t* path) {
	if(!validate_path(path)) return 0;

	// use get_full_path before check
	uint8_t* full_path;
	uint32_t full_size;
	get_full_path(path, &full_path, &full_size);

	struct stat stat_buf;
	int32_t error = stat(full_path, &stat_buf);
	free(full_path);	// free memory allocated for the full file path
	if(error) return 0;
	if(S_ISDIR(stat_buf.st_mode)) return 2;	// this is an existing directory
	if(S_ISREG(stat_buf.st_mode)) return 1;	// this is an existing file
	return 0;	// this is not an existing file or directory
}

// check if path b is higher than path a (path b begins with path a)
uint8_t check_highest_path(uint8_t* a, uint8_t* b) {
	// ignore beginning /
	if(a[0] == '/') a++;
	if(b[0] == '/') b++;
	// work through them both, comparing each character along the way.
	// if the end of either is reached without a character mismatch, path b starts with path a.
	for(uint32_t i = 0; i < 1000; i++) {
		if(a[i] == '\0' || b[i] == '\0') break;
		if(a[i] == '/' && a[i+1] == '\0') break;
		if(b[i] == '/' && b[i+1] == '\0') break;
		if(a[i] != b[i]) return 1; // path b does not start with path a
	}
	return 0;	// path b starts with path a
}

// create some number of directories OR open a file and set the stream FILE* at the first null pointer in file_streams array; this is the file stream ID
uint8_t open_file(uint8_t* path, uint8_t* highest_path, uint16_t* file_id, thread_t* thread) {
	if(!validate_path(path)) return 5;	// the filename contains invalid characters
	if(check_highest_path(highest_path, path)) return 2; // r/w permission denied

	uint32_t path_length = strlen(path)+1;

	uint8_t* full_path;
	uint32_t full_size;
	get_full_path(path, &full_path, &full_size);

	if(path_length == 1) { free(full_path); return 3; } // path was to the root directory so return code for directory already exists
	if(path[path_length-2] != '/') {	// open/create file
		FILE* f = 0;
		if(!check_path_existence(path)) {	// if file doesn't exist, create and open it
			f = fopen(full_path, "w+"); // fails if file's directory doesn't already exist
			if(f) {
				for(uint32_t i = 0; i < 65534; i++)
					if(!thread->file_streams[i]) {
						thread->file_streams[i] = f;
						*file_id = i + 1;
						break;
					}
				free(full_path);
				return 0;	// file was created and opened
			} else switch(errno) {
				// return error codes
				default: free(full_path); return 1;	// unknown error
			}
		} else {	// if file does exist, open it
			f = fopen(full_path, "r+");
			if(f) {
				for(uint32_t i = 0; i < 65534; i++)
					if(!thread->file_streams[i]) {
						thread->file_streams[i] = f;
						*file_id = i + 1;
						break;
					}
				free(full_path);
				return 0;	// file was opened
			} else switch(errno) {
				// return error codes
				default: free(full_path); return 1;	// unknown error
			}
		}
	} else { // create directory; path ends with /
		struct stat stat_buf;
		if(stat(full_path, &stat_buf) == 0 && S_ISDIR(stat_buf.st_mode)) { free(full_path); return 3; }	// all directories exist
		for(uint32_t i = 0; full_path[i] != '\0'; i++)
			if(full_path[i] == '/' && full_path[i+1] != '\0') {
				uint32_t j = i+1;
				for(; full_path[j] != '/'; j++); // j is index of next /
				full_path[j] = '\0';
				if(stat(full_path, &stat_buf) != 0) mkdir(full_path, 0777);
				full_path[j] = '/';
				i = j-1;
			}
		free(full_path);
		return 8;	// directories were created
	}
}

// delete an empty directory or delete a file
uint8_t delete_file(uint8_t* path, uint8_t* highest_path) {
	if(!validate_path(path)) return 5;      // the filename contains invalid characters
	if(check_highest_path(highest_path, path)) return 2; // r/w permission denied

	uint8_t code = check_path_existence(path);
	if(!code) return 1; // unknown error; neither existing file or directory

	uint8_t* full_path;
	uint32_t full_size;
	get_full_path(path, &full_path, &full_size);

	if(code == 1) { // existing file
		// remove file at full_path
		if(remove(full_path) == -1) { free(full_path); return 1; }	// unknown error
		free(full_path);
		return 0;	// no error
	} else if(code == 2) { // existing directory
		// remove directory at full_path
		if(rmdir(full_path) == -1) { free(full_path); return 1; }	// unknown error
		free(full_path);
		return 0;	// no error
	}
}

// takes an n-byte value and swaps the bytes of it (n is 0 to 7 to represent 1-8 bytes; 0 = 8)
uint64_t byteswap(uint64_t val, uint8_t n) {
	if(!val) val = 7;
	switch(n) {
		case 1: val &= 0xFFFF; return (val&0xFF)<<8 | (val&0xFF0)>>8; break;
		case 2: val &= 0xFFFFFF; return (val&0xFF)<<16 | (val&0xFF00) | (val&0xFF0000)>>16; break;
		case 3: val &= 0xFFFFFFFF; return (val&0xFF)<<24 | (val&0xFF00)<<8 | (val&0xFF0000)>>8 | (val&0xFF000000)>>24; break;
		case 4: val &= 0xFFFFFFFFFF; return (val&0xFF)<<32 | (val&0xFF00)<<16 | (val&0xFF000000)>>16 | (val&0xFF00000000)>>32; break;
		case 5: val &= 0xFFFFFFFFFFFF; return (val&0xFF)<<40 | (val&0xFF00)<<24 | (val&0xFF0000)<<8 | (val&0xFF000000)>>8 | (val&0xFF00000000)>>24 | (val&0xFF0000000000)>>40; break;
		case 6: val &= 0xFFFFFFFFFFFFFF; return (val&0xFF)<<48 | (val&0xFF00)<<32 | (val&0xFF0000)<<16 | (val&0xFF000000) | (val&0xFF00000000)>>16 | (val&0xFF0000000000)>>32 | (val&0xFF000000000000)>>48; break;
		case 7: val &= 0xFFFFFFFFFFFFFFFF; return (val&0xFF)<<56 | (val&0xFF00)<<40 | (val&0xFF0000)<<24 | (val&0xFF000000)<<8 | (val&0xFF00000000)>>8 | (val&0xFF0000000000)>>24 | (val&0xFF000000000000)>>40 | (val&0xFF00000000000000)>>56; break;
	}
}

void exec_cycle(thread_t* thread);

void instruction_0(thread_t* thread) { thread->output = &thread->regs[0]; }
void instruction_1(thread_t* thread) { thread->output = &thread->regs[1]; }
void instruction_2(thread_t* thread) { thread->output = &thread->regs[2]; }
void instruction_3(thread_t* thread) { thread->output = &thread->regs[3]; }
void instruction_4(thread_t* thread) { thread->output = &thread->regs[4]; }
void instruction_5(thread_t* thread) { thread->output = &thread->regs[5]; }
void instruction_6(thread_t* thread) { thread->output = &thread->regs[6]; }
void instruction_7(thread_t* thread) { thread->output = &thread->regs[7]; }
void instruction_8(thread_t* thread) { thread->output = &thread->regs[8]; }
void instruction_9(thread_t* thread) { thread->output = &thread->regs[9]; }
void instruction_10(thread_t* thread) { thread->output = &thread->regs[10]; }
void instruction_11(thread_t* thread) { thread->output = &thread->regs[11]; }
void instruction_12(thread_t* thread) { thread->output = &thread->regs[12]; }
void instruction_13(thread_t* thread) { thread->output = &thread->regs[13]; }
void instruction_14(thread_t* thread) { thread->output = &thread->regs[14]; }
void instruction_15(thread_t* thread) { thread->output = &thread->regs[15]; }
void instruction_16(thread_t* thread) { *thread->primary = -(*thread->primary); }
void instruction_17(thread_t* thread) { *thread->secondary = -(*thread->secondary); }
void instruction_18(thread_t* thread) {
	uint8_t n_bits = *thread->secondary & 0x3F;	// rotate left primary by this number of bits
	*thread->primary = (*thread->primary<<n_bits) | (*thread->primary>>(64 - n_bits));
}
void instruction_19(thread_t* thread) {
	uint8_t n_bits = *thread->secondary & 0x3F;	// rotate right primary by this number of bits
	*thread->primary = (*thread->primary>>n_bits) | (*thread->primary<<(64 - n_bits));
}
void instruction_20(thread_t* thread) {
	uint8_t n_bits = *thread->secondary & 0x3F;	// shift left primary by this number of bits
	*thread->primary <<= n_bits;
}
void instruction_21(thread_t* thread) {
	uint8_t n_bits = *thread->primary & 0x3F;	// shift left secondary by this number of bits
	*thread->secondary <<= n_bits;
}
void instruction_22(thread_t* thread) {
	uint8_t n_bits = *thread->secondary & 0x3F;	// shift right primary by this number of bits
	*thread->primary >>= n_bits;
}
void instruction_23(thread_t* thread) {
	uint8_t n_bits = *thread->primary & 0x3F;	// shift right secondary by this number of bits
	*thread->secondary >>= n_bits;
}
void instruction_24(thread_t* thread) {
	uint8_t n_bits = *thread->secondary & 0x3F;	// arithmetic right shift primary by this number of bits
	uint64_t x = *thread->primary, y = n_bits;
	*thread->primary = (x>>y) | -((x & (1LLU << 63)) >> y);
}
void instruction_25(thread_t* thread) {
	uint8_t n_bits = *thread->primary & 0x3F;	// arithmetic right shift of secondary by this number of bits
	uint64_t x = *thread->secondary, y = n_bits;
	*thread->secondary = (x>>y) | -((x & (1LLU << 63)) >> y);
}
void instruction_26(thread_t* thread) { *thread->output = *thread->primary | *thread->secondary; }
void instruction_27(thread_t* thread) { *thread->output = *thread->primary & *thread->secondary; }
void instruction_28(thread_t* thread) { *thread->output = *thread->primary ^ *thread->secondary; }
void instruction_29(thread_t* thread) {		// 32-bit unsigned integer modulo
	if((*thread->secondary&0xFFFFFFFF) == 0) *thread->output = 0;
	else *thread->output = (uint32_t)(*thread->primary&0xFFFFFFFF) % (uint32_t)(*thread->secondary&0xFFFFFFFF);
	*thread->output &= 0xFFFFFFFF;
}
void instruction_30(thread_t* thread) {		// 64-bit unsigned integer modulo
	if(*thread->secondary == 0) *thread->output = 0;
	else *thread->output = *thread->primary % *thread->secondary;
}
void instruction_31(thread_t* thread) { *thread->secondary = *thread->primary; }
void instruction_32(thread_t* thread) { *thread->primary = *thread->secondary; }
void instruction_33(thread_t* thread) { *thread->primary = 0; }
void instruction_34(thread_t* thread) { *thread->secondary = 0; }
void instruction_35(thread_t* thread) { *thread->primary = 0xFFFFFFFFFFFFFFFF; }
void instruction_36(thread_t* thread) { *thread->secondary = 0xFFFFFFFFFFFFFFFF; }
void instruction_37(thread_t* thread) {	// create a thread
	if(!thread->perm_thread_creation) return;
	if(check_segfault(thread, *thread->secondary, 41)) return; // check if reading thread parameters will segfault
	uint8_t* params = read_main_mem(thread, *thread->secondary, 41);

	uint8_t perms = *params++;
	uint64_t privacy_key = ((uint64_t*)params)[0];
	uint64_t segtable_id = ((uint64_t*)params)[1];
	uint64_t min_ins = ((uint64_t*)params)[2];
	uint64_t max_ins = ((uint64_t*)params)[3];
	uint64_t path_addr = ((uint64_t*)params)[4];

	if(check_segfault(thread, path_addr, 1)) return; // address to start of path is outside main memory range; do nothing

	uint8_t* path_str = 0;
	uint32_t path_length = get_string_main_mem(thread, path_addr, &path_str);
	if(path_length == 0) return; // segfault, or path string did not have a terminator

	if(!validate_path(path_str) || check_path_existence(path_str) != 2) {
		free(path_str);
		return;	// highest accessible directory invalid/not existing; do nothing
	}

	if(min_ins > max_ins || max_ins >= SIZE_MAIN_MEM) {
		free(path_str);
		return;
	}

	if(segtable_id == 0 || segtable_id > n_objects) {
		free(path_str);
		return;
	}
	object_t* segtable_object = &objects[segtable_id-1];
	if(segtable_object->type != TYPE_SEGTABLE || segtable_object->privacy_key != thread->privacy_key) {
		free(path_str);
		return;
	}

	if(check_highest_path(thread->highest_dir, path_str)) { // path_str doesn't begin with this thread's highest accessible directory; do nothing
		free(path_str);
		return;
	}

	// thread creation
	uint64_t thread_id = thread->id;
	uint64_t new_id = new_thread(thread_id);
	thread = &threads[thread_id];

	thread_t* created = &threads[new_id];
	created->regs[15] = *thread->primary;
#if SHOW_NEW_THREAD
	printf("created new thread (%d) with PC %lld\n", new_id, created->regs[15]);
#endif
	created->primary = created->regs;
	created->secondary = created->regs;
	created->output = created->regs;
	created->instruction_max = max_ins;
	created->instruction_min = min_ins;
	created->highest_dir = malloc(path_length);	// allocate thread's highest accessible directory string
	memcpy(created->highest_dir, path_str, path_length);	// copy path in main memory to thread's highest accessible directory
	created->highest_dir_length = path_length;
	created->segtable_id = segtable_id;
	created->privacy_key = privacy_key;

	// set the new thread's permissions
	if(perms & 0x1 && thread->perm_screenshot) created->perm_screenshot = 1;
	else created->perm_screenshot = 0;
	if(perms & 0x2 && thread->perm_camera) created->perm_camera = 1;
	else created->perm_camera = 0;
	if(perms & 0x4 && thread->perm_microphones) created->perm_microphones = 1;
	else created->perm_microphones = 0;
	if(perms & 0x8 && thread->perm_networking) created->perm_networking = 1;
	else created->perm_networking = 0;
	if(perms & 0x10 && thread->perm_file_io) created->perm_file_io = 1;
	else created->perm_file_io = 0;
	if(perms & 0x20 && thread->perm_thread_creation) created->perm_thread_creation = 1;
	else created->perm_thread_creation = 0;

	*thread->output = new_id;	// output id of new thread to output register
	thread->end_cyc = 1;

	free(path_str);
}
void instruction_38(thread_t* thread) {
	// detach a thread descendant of this one
	if(*thread->primary == 0 || *thread->primary > n_threads-1) return;
	if(threads[*thread->primary].killed || threads[*thread->primary].detached) return;
	if(!check_descendant(thread, &threads[*thread->primary])) return;
	threads[*thread->primary].detached = 1;
}
void instruction_39(thread_t* thread) {
	// destroy a thread descendant of this one
	if(*thread->primary == 0 || *thread->primary > n_threads-1) return;
	if(threads[*thread->primary].killed) return;
	if(!check_descendant(thread, &threads[*thread->primary])) return;
	kill_thread(&threads[*thread->primary]);
}
void instruction_40(thread_t* thread) {
	if(*thread->primary == 0 || *thread->primary > n_threads-1) return;
	if(threads[*thread->primary].killed || threads[*thread->primary].detached) return;
	if(!check_descendant(thread, &threads[*thread->primary])) return;
	thread->joining = *thread->primary;
	thread->end_cyc = 1;
}
void instruction_41(thread_t* thread) {
	if(*thread->primary != 0) {
		struct timespec tm;
		clock_gettime(CLOCK_REALTIME, &tm);
		thread->sleep_duration_ns = *thread->primary;
		thread->sleep_start_ns = (tm.tv_sec-start_tm.tv_sec)*NS_PER_SEC + (tm.tv_nsec-start_tm.tv_nsec);

		// if all threads are currently sleeping, do nanosleep for the minimum duration
		uint64_t min = *thread->primary;
		for(uint32_t i = 0; i < n_threads; i++) {
			thread_t* thread = &threads[i];
			if(thread->killed) continue;
			if(thread->sleep_duration_ns == 0) break;
			if(thread->sleep_duration_ns < min) min = thread->sleep_duration_ns;
			if(i == n_threads-1) {
				struct timespec tm;
				tm.tv_sec = min / NS_PER_SEC;
				tm.tv_nsec = min % NS_PER_SEC;
				nanosleep(&tm,&tm);
			}
		}
	}
	thread->end_cyc = 1;
}
void instruction_42(thread_t* thread) {
	if(*thread->primary == 0) *thread->output = thread->id;
	else if(*thread->primary == 1) *thread->output = thread->perm_screenshot;
	else if(*thread->primary == 2) *thread->output = thread->perm_camera;
	else if(*thread->primary == 3) *thread->output = thread->perm_microphones;
	else if(*thread->primary == 4) *thread->output = thread->perm_networking;
	else if(*thread->primary == 5) *thread->output = thread->perm_file_io;
	else if(*thread->primary == 6) *thread->output = thread->perm_thread_creation;
	else if(*thread->primary == 7) *thread->output = thread->instruction_min;
	else if(*thread->primary == 8) *thread->output = thread->instruction_max;
	else if(*thread->primary == 9) *thread->output = thread->highest_dir_length;
	else if(*thread->primary == 10) {
		if(check_segfault(thread, *thread->secondary, thread->highest_dir_length)) return;
		write_main_mem(thread, *thread->secondary, thread->highest_dir, thread->highest_dir_length);
	}
	else if(*thread->primary == 11) *thread->output = thread->privacy_key;
	else if(*thread->primary == 12) {
		if(check_segfault(thread, *thread->secondary, 9)) return;
		uint64_t thread_id = read_main_mem_val(thread, *thread->secondary, 8);
		uint8_t update_byte = read_main_mem_val(thread, *thread->secondary+8, 1);	// byte specifying what to update
		if(thread_id > n_threads-1) return;
		thread_t* update = &threads[thread_id];// thread being updated
		if(update_byte == 9 && thread_id == 0 && thread->id != 0) return;	// thread 0 can update its own privacy key
		else if(!check_descendant(thread, update)) return;	// thread whose information is being updated is not a descendant of this one; do nothing
		if(update_byte > 9) return;
		if(update_byte < 6) {
			if(check_segfault(thread, *thread->secondary, 10)) return;	// update data is out of main memory range
		} else if(check_segfault(thread, *thread->secondary, 17)) return;	// update data is out of main memory range
		uint64_t value = read_main_mem_val(thread, *thread->secondary+9, 1);
		switch(update_byte) {
			case 0:	if(value && thread->perm_screenshot) update->perm_screenshot = 1; // update screenshot permission
				else if(!value) update->perm_screenshot = 0; break;
			case 1: if(value && thread->perm_camera) update->perm_camera = 1; // update camera permission
				else if(!value) update->perm_camera = 0; break;
			case 2: if(value && thread->perm_microphones) update->perm_microphones = 1; // update microphones permission
				else if(!value) update->perm_microphones = 0; break;
			case 3: if(value && thread->perm_networking) update->perm_networking = 1; // update networking permission
				else if(!value) update->perm_networking = 0; break;
			case 4: if(value && thread->perm_file_io) update->perm_file_io = 1; // update file IO permission
				else if(!value) update->perm_file_io = 0; break;
			case 5: if(value && thread->perm_thread_creation) update->perm_thread_creation = 1; // update thread creation permission
				else if(!value) update->perm_thread_creation = 0; break;
		}
		value = read_main_mem_val(thread, *thread->secondary+9, 8);
		switch(update_byte) {
			case 6:	if(value <= update->instruction_max && value < SIZE_MAIN_MEM) update->instruction_min = value; break;
			case 7: if(value >= update->instruction_min && value < SIZE_MAIN_MEM) update->instruction_max = value; break;
			case 8: // update highest directory path
				;
				uint8_t* path_str = 0;
				uint32_t path_length = get_string_main_mem(thread, value, &path_str);
				if(path_length == 0) return; // segfault, or path string did not have a terminator
				if(check_highest_path(thread->highest_dir, path_str)) return; // path_str doesn't begin with this thread's highest accessible directory; do nothing
				update->highest_dir = realloc(update->highest_dir, path_length);	// reallocate highest directory string for descendant thread
				memcpy(update->highest_dir, path_str, path_length);	// update highest directory string of descendant thread
				update->highest_dir_length = strlen(path_str)+1;
				free(path_str);
				break;
			case 9: update->privacy_key = value;
		}
	} else if(*thread->primary == 13) {
		// get the 'killed' value from a descendant thread
		if(*thread->secondary > n_threads-1) return;
		thread_t* descendant = &threads[*thread->secondary];
		if(!check_descendant(thread, descendant)) return;
		*thread->output = descendant->killed;
	}
}
void instruction_43(thread_t* thread) {
	uint8_t z = (thread->regs[13] & SR_BIT_Z) ? 1 : 0;
	uint8_t c = (thread->regs[13] & SR_BIT_C) ? 1 : 0;
	uint8_t v = (thread->regs[13] & SR_BIT_V) ? 1 : 0;
	uint8_t n = (thread->regs[13] & SR_BIT_N) ? 1 : 0;
	uint8_t output = 0;
	switch(*thread->primary) {
		case 0: if(z) output = 1; break;
		case 1: if(!z) output = 1; break;
		case 2: if(c) output = 1; break;
		case 3: if(!c) output = 1; break;
		case 4: if(n) output = 1; break;
		case 5: if(!n) output = 1; break;
		case 6: if(v) output = 1; break;
		case 7: if(!v) output = 1; break;
		case 8: if(c && !z) output = 1; break;
		case 9: if(!c || z) output = 1; break;
		case 10: if(n == v) output = 1; break;
		case 11: if(n != v) output = 1; break;
		case 12: if(!z && (n == v)) output = 1; break;
		case 13: if(z || (n!=v)) output = 1; break;
		default: return;
	}
	*thread->output = output;
}
void instruction_44(thread_t* thread) {
	uint64_t new_lr = thread->regs[15] + 1;
	thread->regs[15] = *thread->primary;
	thread->regs[14] = new_lr;
	thread->end_cyc = 1;
}
void instruction_45(thread_t* thread) {
	if(thread->regs[13] & SR_BIT_Z) {	// if Z is set
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_46(thread_t* thread) {
	if(!(thread->regs[13] & SR_BIT_Z)) {        // if Z is not set
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_47(thread_t* thread) {
	if(thread->regs[13] & SR_BIT_C) {	// if C is set
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_48(thread_t* thread) {
	if(!(thread->regs[13] & SR_BIT_C)) {	// if C is not set
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_49(thread_t* thread) {
	if(thread->regs[13] & SR_BIT_N) {	// if N is set
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_50(thread_t* thread) {
	if(!(thread->regs[13] & SR_BIT_N)) {	// if N is not set
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_51(thread_t* thread) {
	if(thread->regs[13] & SR_BIT_V) {	// if V is set
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_52(thread_t* thread) {
	if(!(thread->regs[13] & SR_BIT_V)) {	// if V is not set
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_53(thread_t* thread) {
	if(thread->regs[13] & SR_BIT_C && !(thread->regs[13] & SR_BIT_Z)) {	// if C is set and Z is not set
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_54(thread_t* thread) {
	if(!(thread->regs[13] & SR_BIT_C) || thread->regs[13] & SR_BIT_Z) {	// if C is not set or Z is set
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_55(thread_t* thread) {
	if(((thread->regs[13] & SR_BIT_N) & (thread->regs[13] & SR_BIT_V)) == (SR_BIT_N | SR_BIT_V)) {	// if N == V
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_56(thread_t* thread) {
	if(!(thread->regs[13] & SR_BIT_N) != !(thread->regs[13] & SR_BIT_V)) {	// if N != V
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_57(thread_t* thread) {
	if(!(thread->regs[13] & SR_BIT_Z) && (((thread->regs[13] & SR_BIT_N) & (thread->regs[13] & SR_BIT_V)) == (SR_BIT_N | SR_BIT_V))) {	// if Z is not set and N == V
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_58(thread_t* thread) {
	if(thread->regs[13] & SR_BIT_Z || !(thread->regs[13] & SR_BIT_N) != !(thread->regs[13] & SR_BIT_V)) {	// if Z is set or N != V
		uint64_t new_lr = thread->regs[15] + 1;
		thread->regs[15] = *thread->primary;
		thread->regs[14] = new_lr;
		thread->end_cyc = 1;
	}
}
void instruction_59(thread_t* thread) {
	thread->regs[15] = thread->regs[14];	// set PC to LR
	thread->end_cyc = 1;
}
void instruction_60(thread_t* thread) {
	if(!thread->perm_file_io) return;
	// open a file; path addressed by primary register
	uint8_t* path_str = 0;
	uint32_t path_length = get_string_main_mem(thread, *thread->primary, &path_str);
	if(path_length == 0) { update_stream_open(thread); return; } // segfault, or path string did not have a terminator
	if(!validate_path(path_str)) { update_stream_open(thread); return; }
	uint8_t path_type = check_path_existence(path_str);

	uint16_t stream_id = 0;
	uint8_t code = 8;
	if(path_type == 2 && !*thread->output) { // existing dir; move the file (path string to file addressed by secondary)
		uint8_t* full_path, *full_fpath;
		uint32_t full_size;
		get_full_path(path_str, &full_path, &full_size);
		if(!get_string_main_mem(thread, *thread->secondary, &path_str)) { update_stream_open(thread); return; }  // segfault, or path string did not have a terminator
		if(!validate_path(path_str)) { update_stream_open(thread); return; }
		get_full_path(path_str, &full_fpath, &full_size);

		if(check_path_existence(path_str) == 1) { // full_fpath must be path to a file to move
			if(full_path[strlen(full_path)-1] != '/') {
				uint32_t length = strlen(full_path);
				full_path = realloc(full_path, length+2);
				full_path[length] = '/'; // add / to end of full_path
				full_path[length+1] = '\0';
			}
			uint32_t len = 0, i = 0; // start of filename in full_fpath, length of filename
			for(i = strlen(full_fpath); full_fpath[i] != '/'; i--) len++;
			i++, len--; // i is index after /, len is number of bytes after /
			full_path = realloc(full_path, strlen(full_path)+len+1); // add the filename to full_path before rename
			memmove(full_path+strlen(full_path), full_fpath+i, len+1);
			if(rename(full_fpath, full_path) != 0) code = 1;	// move file to directory
		}
		free(full_path);
		free(full_fpath);
	} else if(!*thread->output) code = open_file(path_str, thread->highest_dir, &stream_id, thread); // create file/directories
	else if(*thread->output)			// check if the file path specified by primary register exists
		*thread->output = path_type > 0;	// output register = 1 if existing file or directory, and 0 otherwise
	// set error code in SR if the code isn't success, otherwise set file ID in SR
	thread->regs[13] = thread->regs[13] & (~0xFE00ull);	// clear the file IO error bits
	switch(code) {
		case 0: 
			*thread->output = stream_id;
			thread->regs[13] = (thread->regs[13] & (~0xFFFF0000000ull)) | ((uint64_t)stream_id << 28);
			break; // no error; set current stream ID in SR and output the stream ID
		case 1: thread->regs[13] |= 0x8000; break; // unrecognized error
		case 2: thread->regs[13] |= 0x4000; break; // r/w permission error
		case 3: thread->regs[13] |= 0x2000; break; // directory exists error
		case 4: thread->regs[13] |= 0x1000; break; // filename too long error
		case 5: thread->regs[13] |= 0x800; break; // the file path contains invalid characters
		case 6: thread->regs[13] |= 0x400; break; // too many files already open
		case 7: thread->regs[13] |= 0x200; break; // the file path is read-only
		case 8: break; // success
	}
	update_stream_open(thread);
}
void instruction_61(thread_t* thread) {
	if(!thread->perm_file_io) return;
	// delete a file or empty directory; path addressed by primary register
	uint8_t* path_str = 0;
	uint32_t path_length = get_string_main_mem(thread, *thread->primary, &path_str);
	if(path_length == 0) { update_stream_open(thread); return; } // segfault, or path string did not have a terminator
	uint8_t code = delete_file(path_str, thread->highest_dir);
	thread->regs[13] = thread->regs[13] & (~0xFE00ull);	// clear the file IO error bits
	if(code == 1) thread->regs[13] |= 0x8000; // unrecognized error
	free(path_str);
	update_stream_open(thread);
}
void instruction_62(thread_t* thread) {
	if(!thread->perm_file_io) return;
	// close a file stream named by primary register
	uint16_t stream_id = *thread->primary;
	if(*thread->primary == 0) stream_id = (thread->regs[13] & 0xFFFF0000000)>>28;
	if(stream_id > 65535 || stream_id == 0) { update_stream_open(thread); return; }
	FILE* f = thread->file_streams[stream_id-1];
	if(f) fclose(f);
	thread->file_streams[stream_id-1] = 0;
	update_stream_open(thread);
}
void instruction_63(thread_t* thread) {
	thread->regs[13] = (thread->regs[13] & (~0xFFFF0000000ull)) | ((*thread->primary & 0xFFFF) << 28);
	update_stream_open(thread);
}
void instruction_64(thread_t* thread) { // write to file
	if(!thread->perm_file_io) return;
	uint64_t data_addr = *thread->primary;
	if(check_segfault(thread, data_addr, *thread->output+1)) { update_stream_open(thread); return; }
	uint8_t* data = read_main_mem(thread, data_addr, *thread->output+1); // data to write to file
	uint64_t data_size = *thread->output + 1;
	uint16_t stream_id = (thread->regs[13] & 0xFFFF0000000)>>28;
	uint64_t file_size = 0;
	if(!stream_id) { update_stream_open(thread); return; }	// file stream id is 0 (which is reserved)
	if(thread->file_streams[stream_id-1]) { // get the size of the file
		uint64_t prev = ftell(thread->file_streams[stream_id-1]);
		fseek(thread->file_streams[stream_id-1], 0L, SEEK_END);
		file_size = ftell(thread->file_streams[stream_id-1]);
		fseek(thread->file_streams[stream_id-1], prev, SEEK_SET);
	} else { update_stream_open(thread); return; } // this file is not open
	if(*thread->secondary + data_size - 1 > file_size) { update_stream_open(thread); return; } // data write out of range of file
	// write to file "data", for number of bytes specified by data_size, at byte addressed by secondary reg
	fseek(thread->file_streams[stream_id-1], *thread->secondary, SEEK_SET);
	fwrite(data, 1, data_size, thread->file_streams[stream_id-1]);
	fseek(thread->file_streams[stream_id-1], 0, SEEK_SET);
	free(data);
	update_stream_open(thread);
}
void instruction_65(thread_t* thread) { // read from file
	if(!thread->perm_file_io) return;
	uint64_t output_addr = *thread->primary;
	uint64_t output_size = *thread->output + 1;
	if(check_segfault(thread, output_addr, output_size));
	uint8_t* output = malloc(output_size); // temp. storage for read file data
	uint16_t stream_id = (thread->regs[13] & 0xFFFF0000000)>>28;
	uint64_t file_size = 0;
	if(!stream_id) { update_stream_open(thread); return; }  // file stream id is 0 (which is reserved)
	if(thread->file_streams[stream_id-1]) { // get the size of the file
		fseek(thread->file_streams[stream_id-1], 0L, SEEK_END);
		file_size = ftell(thread->file_streams[stream_id-1]);
		fseek(thread->file_streams[stream_id-1], 0L, SEEK_SET);
	} else { update_stream_open(thread); return; } // this file is not open
	if(*thread->secondary + output_size - 1 > file_size) { update_stream_open(thread); return; } // data read out of range of file
	// read from file to "output", for number of bytes specified by output_size, at byte specified by secondary reg
	fseek(thread->file_streams[stream_id-1], *thread->secondary, SEEK_SET);
	fread(output, 1, output_size, thread->file_streams[stream_id-1]);
	fseek(thread->file_streams[stream_id-1], 0L, SEEK_SET);
	write_main_mem(thread, output_addr, output, output_size);
	free(output);
	update_stream_open(thread);
}
void instruction_66(thread_t* thread) { // resize a file or get its size
	if(!thread->perm_file_io) return;
	uint16_t stream_id = (thread->regs[13] & 0xFFFF0000000)>>28;
	if(!stream_id) { update_stream_open(thread); return; } // no file stream is set as current file stream
	if(thread->file_streams[stream_id-1]) { // file stream is open
		if(*thread->primary) { // resize the file to *thread->primary bytes
			uint64_t fd = fileno(thread->file_streams[stream_id-1]);
			ftruncate(fd, *thread->primary);
		} else { // get file size
			fseek(thread->file_streams[stream_id-1], 0L, SEEK_END);
			*thread->output = ftell(thread->file_streams[stream_id-1]);
			fseek(thread->file_streams[stream_id-1], 0L, SEEK_SET);
		}
	}
	update_stream_open(thread);
}
void instruction_67(thread_t* thread) {
	*thread->output = 1; // this VM currently does not support any external drives therefore 1 is the number of available drives
}
void instruction_68(thread_t* thread) {
	// output 16-bit drive types to primary register address
	if(*thread->secondary == 0) return; // element output limit is set to 0
	if(check_segfault(thread, *thread->primary, 17)) return;
	write_main_mem_val(thread, *thread->primary, 1, 1); // drive type byte; HDD/SSD is assumed
	uint64_t drive_capacity_bytes = 0;
	uint64_t drive_available_bytes = 0;
	// get drive info through df command
	FILE* fp = popen("/bin/df -k -P .", "r");
	if(fp) {
		char out[1024];
		while(fgets(out, sizeof(out), fp)); // go to last line
		char* t = strtok(out, " ");
		strtok(0, " "); t = strtok(0, " ");
		uint64_t blocks = strtoul(t,0,0); // blocks are 1024 bytes (-k command parameter)
		uint64_t used = blocks*1024;
		t = strtok(0, " ");
		blocks = strtoul(t,0,0);
		drive_available_bytes = blocks*1024;
		drive_capacity_bytes = drive_available_bytes+used;
		pclose(fp);
	}
	write_main_mem_val(thread, *thread->primary+1, drive_capacity_bytes, 8);
	write_main_mem_val(thread, *thread->primary+9, drive_available_bytes, 8);
}
void instruction_69(thread_t* thread) {
	uint64_t path_addr = *thread->primary;
	uint8_t* path_str = 0;
	uint32_t path_length = get_string_main_mem(thread, path_addr, &path_str);
	if(path_length == 0) return; // segfault, or path string did not have a terminator
	if(check_path_existence(path_str) != 2) { free(path_str); return; } // not an existing directory; do nothing

	uint16_t n_directories = 0, n_files = 0; // number of directories and files at directory whose path is specified by path_str
	uint64_t dir_bytes = 0, file_bytes = 0; // number of bytes for directory and file strings (incl. null characters)
	uint8_t* dirs, *files;

	DIR* dir;
	dirent* dir_entry;
	uint8_t* full_path;
	uint32_t full_size;
	get_full_path(path_str, &full_path, &full_size);

	if(!(dir=opendir(full_path))) return; // could not open the directory for directory entry reading
	while(dir_entry = readdir(dir)) { // dir_entry is a file or directory in dir
		uint32_t filename_length = strlen(dir_entry->d_name)+1;
		uint8_t* entry_path = calloc(1,strlen(full_path) + filename_length);
		memcpy(entry_path, full_path, strlen(full_path));
		memcpy(entry_path+strlen(full_path), dir_entry->d_name, strlen(dir_entry->d_name));

		struct stat stat_buf;
		int32_t error = stat(entry_path, &stat_buf);
		free(entry_path);
		if(error) continue;
		if(S_ISREG(stat_buf.st_mode)) {
			files = realloc(files, file_bytes + filename_length + 8);
			memcpy(files+file_bytes, dir_entry->d_name, filename_length);
			*(uint64_t*)(files+file_bytes+filename_length) = stat_buf.st_size;
			file_bytes += filename_length + 8;
			n_files++;
		} else if(S_ISDIR(stat_buf.st_mode)) {
			dirs = realloc(dirs, dir_bytes + filename_length);
			memcpy(dirs+dir_bytes, dir_entry->d_name, filename_length);
			dir_bytes += filename_length;
			n_directories++;
		}
	}
	closedir(dir);
	free(full_path);

	uint64_t output_addr = *thread->secondary;
	uint64_t output_size = 16+dir_bytes+file_bytes;
	if(check_segfault(thread, output_addr, output_size)) return;

	uint64_t* output = malloc(output_size);
	output[0] = dir_bytes;
	output[1] = file_bytes;
	if(dir_bytes)  memcpy(output+2, dirs, dir_bytes);
	if(file_bytes) memcpy((uint8_t*)(output+2)+dir_bytes, files, file_bytes);
	write_main_mem(thread, output_addr, (uint8_t*)output, output_size);

	free(output);
	free(dirs);
	free(files);
}
void instruction_70(thread_t* thread) {
	uint64_t path_addr = *thread->primary;
	uint8_t* path_str = 0;
	uint32_t path_length = get_string_main_mem(thread, path_addr, &path_str);
	if(path_length == 0) return; // segfault, or path string did not have a terminator
	if(check_path_existence(path_str) != 2) return; // not an existing directory; do nothing

	DIR* dir;
	dirent* dir_entry;
	uint8_t* full_path;
	uint32_t full_size;
	get_full_path(path_str, &full_path, &full_size);
	uint64_t bytes = 0;	// number of bytes for filename strings of directories and files

	if(!(dir=opendir(full_path))) return; // could not open the directory for directory entry reading
	while(dir_entry = readdir(dir)) { // dir_entry is a file or directory in dir
		uint32_t filename_length = strlen(dir_entry->d_name)+1;
		bytes += filename_length;
		
		uint8_t* entry_path = calloc(1,strlen(full_path) + filename_length);
		memcpy(entry_path, full_path, strlen(full_path));
		memcpy(entry_path+strlen(full_path), dir_entry->d_name, strlen(dir_entry->d_name));
		
		struct stat stat_buf;
		int32_t error = stat(entry_path, &stat_buf);
		free(entry_path);
		if(error) continue;
		if(S_ISREG(stat_buf.st_mode)) bytes += 8;
	}
	closedir(dir);
	free(full_path);
	*thread->output = 16+bytes;
}
void instruction_71(thread_t* thread) { if(!*thread->primary) thread->regs[13] &= (~0x7F80000ull); }
void instruction_72(thread_t* thread) {	// generate an object
	// object generation instruction; object_t has pointers to all kinds of objects (specifically GL objects) and a uint8_t type member stating what type of object it is.

	// store ID of new object in output register. if object creation fails nothing will happen.

	// ray tracing objects will not be usable in this implementation so don't worry about those things.

	if(*thread->primary > 35) return;	// do nothing
	uint64_t object_id = new_object();	// create a new object and get the ID
	object_t* object = &objects[object_id-1];	// get a pointer to the new object
	memset(object,0,sizeof(object_t));

	#define CLEAN_RETURN { n_objects--; objects = realloc(objects, sizeof(object_t)*n_objects); return; }

	object->type = *thread->primary;
	object->privacy_key = thread->privacy_key;
	switch(object->type) {
		case TYPE_CBO: object->cbo.cmds = 0; object->cbo.size = 0; object->cbo.pipeline_type = 2; for(uint32_t i = 0; i < 4; i++) object->cbo.bindings[i] = 0; break;
		case TYPE_VAO:
			if(check_segfault(thread, *thread->secondary, 10)) CLEAN_RETURN;
			uint16_t n_attribs = read_main_mem_val(thread, *thread->secondary, 2) + 1; // number of vertex attribs
			if(check_segfault(thread, *thread->secondary, 10+n_attribs*11)) CLEAN_RETURN;
			uint8_t success = 0;
			object->vao.stride = 0;
			object->vao.offsets = 0;
			object->vao.formats = 0;
			object->vao.n_attribs = 0;
			object->vao.gl_vao_ids = 0;
			object->vao.vbo_ids = 0;
			object->vao.n_vaos = 0;
			uint8_t* data = read_main_mem(thread, *thread->secondary, 10+n_attribs*11);
			create_vao(&object->vao, data, &success);
			free(data);
			if(!success) CLEAN_RETURN;
			break;
		case TYPE_VBO: glGenBuffers(1, &object->gl_buffer); break;
		case TYPE_IBO: glGenBuffers(1, &object->gl_buffer); break;
		case TYPE_TBO:
			if(*thread->secondary > 13) CLEAN_RETURN;	// not a valid texture format
			object->tbo.format = *thread->secondary;
			object->tbo.level_widths = 0;
			object->tbo.level_heights = 0;
			object->tbo.n_levels = 0;
			glGenTextures(1, &object->tbo.gl_buffer);
			break;
		case TYPE_FBO: object->fbo.width = 0; object->fbo.height = 0; glGenFramebuffers(1, &object->fbo.gl_buffer); break;
		case TYPE_UBO: object->ubo.data = 0; object->ubo.size = 0; break;
		case TYPE_SBO: object->sbo.data = 0; object->sbo.size = 0; break;
		case TYPE_DBO: object->dbo.data = 0; object->dbo.size = 0; break;
		case TYPE_SAMPLER_DESC: object->s_mode = 0; object->t_mode = 0; object->min_filter = 0; object->mag_filter = 0; object->object_id = 0; break;	// TBO descriptor
		case TYPE_IMAGE_DESC: object->image_level = 0; object->object_id = 0; break; // image descriptor
		case TYPE_UNIFORM_DESC: object->object_id = 0; break;	// UBO descriptor
		case TYPE_STORAGE_DESC: object->object_id = 0; break;	// SBO descriptor
		case TYPE_AS_DESC: break;	// acceleration structure descriptor; not supported
		case TYPE_DSET: // create descriptor set
			;
			uint64_t layout_id = *thread->secondary;
			if(layout_id == 0 || layout_id > n_objects) CLEAN_RETURN;
			object_t* layout_object = &objects[layout_id-1];
			if(layout_object->deleted) CLEAN_RETURN;
			if(layout_object->privacy_key != thread->privacy_key) CLEAN_RETURN;
			if(layout_object->type != TYPE_SET_LAYOUT) CLEAN_RETURN;
			set_layout_t* layout = &layout_object->set_layout;
			desc_set_t* set = &object->dset;
			set->layout_id = layout_id;
			set->n_bindings = layout->n_binding_points;
			set->bindings = malloc(sizeof(desc_binding_t)*(object->dset.n_bindings+1));
			// init each of the set's desc_binding_t structures located in dset.bindings according to the set_layout_t 'layout'
			for(uint32_t i = 0; i < layout->n_binding_points+1; i++) {
				desc_binding_t* bind_point = &set->bindings[i];
				bind_point->binding_number = layout->binding_numbers[i];
				bind_point->binding_type = layout->binding_types[i];
				bind_point->object_ids = calloc(layout->n_descs[i], sizeof(uint32_t));	// all will be init. to 0
				if(bind_point->binding_type == 2) { // sampler descriptor binding point
					bind_point->min_filters = calloc(layout->n_descs[i], 1);
					bind_point->mag_filters = calloc(layout->n_descs[i], 1);
					bind_point->s_modes = calloc(layout->n_descs[i], 1);
					bind_point->t_modes = calloc(layout->n_descs[i], 1);
				}
				bind_point->n_descs = layout->n_descs[i];
			}
			break;
		case TYPE_SET_LAYOUT: // create descriptor set layout
			// 4 bytes: # groups (0 corresponds to 1)
			// each group: 4 bytes binding point number, 1 byte type of descriptor binding (2=sampler descriptor), 2 bytes if sampler descriptor
			if(check_segfault(thread, *thread->secondary, 4)) CLEAN_RETURN;
			uint32_t n_binding_points = read_main_mem_val(thread, *thread->secondary, 4) + 1;	// number of binding points
			uint32_t* bind_nums = calloc(n_binding_points,4);
			uint64_t addr = *thread->secondary + 4;
			for(uint32_t i = 0; i < n_binding_points; i++) {
				if(check_segfault(thread, addr, 5)) { free(bind_nums); CLEAN_RETURN; }
				bind_nums[i] = read_main_mem_val(thread, addr, 4);
				for(uint32_t j = 0; j < i; j++) // check all previous indices
					if(bind_nums[i] == bind_nums[j]) { free(bind_nums); CLEAN_RETURN; } // repeated binding num
				addr += 4; addr++;	// binding point number, type
				if(read_main_mem_val(thread, addr-1, 1) > 4) { free(bind_nums); CLEAN_RETURN; }	// binding type does not exist
				if(read_main_mem_val(thread, addr-1, 1) == 2) { // if sampler descriptor
					if(check_segfault(thread, addr, 2)) { free(bind_nums); CLEAN_RETURN; }
					if(read_main_mem_val(thread, addr, 2) == 0) { free(bind_nums); CLEAN_RETURN; }	// descriptor count cannot be 0
					addr += 2;	// number of descriptors at binding point
				}
			}
			uint32_t size = addr - *thread->secondary;
			data = malloc(size);
			data = read_main_mem(thread, *thread->secondary, size);
			create_set_layout(&object->set_layout, data, thread->id);
			free(data);
			break;
		case TYPE_VSH: object->shader.src = 0; object->shader.size = 0; object->shader.type = 0; break;	// vertex shader
		case TYPE_PSH: object->shader.src = 0; object->shader.size = 0; object->shader.type = 1; break;	// pixel shader
		case TYPE_CSH: object->shader.src = 0; object->shader.size = 0; object->shader.type = 2; break;	// compute shader
		case TYPE_RASTER_PIPE: // rasterization pipeline
			// pipeline creation info is 52 bytes before accounting for descriptor set layout IDs; # of sets is at info[27]
			if(check_segfault(thread, *thread->secondary, 52)) CLEAN_RETURN;
			uint16_t n_sets = read_main_mem_val(thread, *thread->secondary+27, 2); // offset of 27
			if(n_sets > 256) CLEAN_RETURN;
			if(check_segfault(thread, *thread->secondary, 52+n_sets*8)) CLEAN_RETURN;
			object->pipeline.type = 0;
			success = 0;
			uint8_t* info = read_main_mem(thread, *thread->secondary, 52+n_sets*8);
			create_pipeline(&object->pipeline, info, &success, thread);
			free(info);
			if(!success) CLEAN_RETURN;	// pipeline creation failed; nothing will happen
			break;
		case TYPE_RT_PIPE: break; // ray tracing pipeline; not supported
		case TYPE_COMPUTE_PIPE: // compute pipeline
			if(check_segfault(thread, *thread->secondary, 11)) CLEAN_RETURN;
			n_sets = read_main_mem_val(thread, *thread->secondary+9, 2);
			if(check_segfault(thread, *thread->secondary, 11+n_sets*8)) CLEAN_RETURN;
			object->pipeline.type = 2;
			success = 0;
			info = read_main_mem(thread, *thread->secondary, 11+n_sets*8);
			create_pipeline(&object->pipeline, info, &success, thread);
			free(info);
			if(!success) CLEAN_RETURN;	// pipeline creation failed; nothing will happen
			break;
		case TYPE_VID_DATA: memset(&object->vid_data, 0, sizeof(vid_data_t)); break;
		case TYPE_SEGTABLE: object->segtable.segments = 0; object->segtable.n_segments = 0; break; // segment table objects
		default:
			object->shader.type = 3; // in case it was one of the ray tracing shaders; shader object with a type value of 3 signifies it is one of the (unsupported) ray tracing shaders if the object is a shader object
			object->pipeline.type = 3; // in case it was a ray tracing pipeline; pipeline object with a type value of 3 signifies it is one of the (unsupported) ray tracing pipelines if the object is a pipeline object
	}

	// if the function reaches here, object creation was a success.
	*thread->output = object_id;
}
void instruction_73(thread_t* thread) {	// delete an object
	// Delete an object previously created by instruction 72 with ID specified by the primary register. Will free all of its contents. Does nothing if the primary register is 0 or the object’s buffer is mapped.
	if(*thread->primary == 0 || *thread->primary > n_objects) return;	// object does not exist

	object_t* object = &objects[*thread->primary-1];
	if(object->deleted || object->mapped_address) return;	// object had already been deleted or its buffer is mapped
	if(object->privacy_key != thread->privacy_key) return;
	switch(object->type) {
		case TYPE_VAO: case TYPE_VBO: case TYPE_IBO:
			glDeleteBuffers(1, &object->gl_buffer); break;
		case TYPE_TBO: glDeleteTextures(1, &object->tbo.gl_buffer); break;
		case TYPE_FBO: glDeleteFramebuffers(1, &object->fbo.gl_buffer); break;
		case TYPE_UBO: if(object->ubo.data) free(object->ubo.data); break;
		case TYPE_SBO: if(object->sbo.data) free(object->sbo.data); break;
		case TYPE_DBO: if(object->dbo.data) free(object->dbo.data); break;
		case TYPE_VID_DATA:
			for(uint32_t i = 0; i < object->vid_data.n_frames; i++)
				if(object->vid_data.frames[i])
					free(object->vid_data.frames[i]);
			if(object->vid_data.n_frames)
				free(object->vid_data.frames);
			break;
		case TYPE_SEGTABLE:
			for(uint32_t i = 1; i < n_threads; i++)
				if(threads[i].segtable_id == *thread->primary)
					threads[i].segtable_id = 0;
			break;
	}
	object->deleted = 1;
}
void instruction_74(thread_t* thread) {	// bind an object
	uint8_t type;
	if(*thread->primary == 0)
		type = *thread->secondary & 0x3F;
	else if(*thread->primary <= n_objects) {
		// object exists
		object_t* object = &objects[*thread->primary-1];
		if(object->deleted) return;	// object previously deleted
		if(object->privacy_key != thread->privacy_key) return;
		type = object->type;
	}
	else return;

	if(type > 35) return;	// the type specified by the secondary register is not a valid object type

	switch(type) {
		case TYPE_CBO: thread->bindings.cbo_binding = *thread->primary; break;	// set CBO binding
		case TYPE_VAO: thread->bindings.vao_binding = *thread->primary; break;	// set VAO binding
		case TYPE_VBO: thread->bindings.vbo_binding = *thread->primary; break;	// set VBO binding
		case TYPE_IBO: thread->bindings.ibo_binding = *thread->primary; break;	// set IBO binding
		case TYPE_TBO: thread->bindings.tbo_binding = *thread->primary; break;	// set TBO binding
		case TYPE_FBO: thread->bindings.fbo_binding = *thread->primary; break;	// set FBO binding
		case TYPE_UBO: thread->bindings.ubo_binding = *thread->primary; break;	// set UBO binding
		case TYPE_SBO: thread->bindings.sbo_binding = *thread->primary; break;	// set SBO binding
		case TYPE_DBO: thread->bindings.dbo_binding = *thread->primary; break;	// set DBO binding
		case TYPE_SAMPLER_DESC: thread->bindings.sampler_desc_binding = *thread->primary; break;	// set TBO descriptor binding
		case TYPE_UNIFORM_DESC: thread->bindings.uniform_desc_binding = *thread->primary; break;	// set UBO descriptor binding
		case TYPE_STORAGE_DESC: thread->bindings.storage_desc_binding = *thread->primary; break;	// set SBO descriptor binding
		case TYPE_IMAGE_DESC: thread->bindings.image_desc_binding = *thread->primary; break;		// set image descriptor binding
		case TYPE_DSET: thread->bindings.desc_set_binding = *thread->primary; break; // set descriptor set binding
		case TYPE_VSH: case TYPE_PSH: case TYPE_CSH:
			thread->bindings.shader_binding = *thread->primary; break;// set shader object binding
		case TYPE_RASTER_PIPE: case TYPE_COMPUTE_PIPE:
			thread->bindings.pipeline_binding = *thread->primary; break;// set pipeline object binding
		case TYPE_VID_DATA: thread->bindings.vid_data_binding = *thread->primary; break;	// set video data object binding
		case TYPE_SEGTABLE: thread->bindings.segtable_binding = *thread->primary; break;	// set segment table object binding
	}
}
void instruction_75(thread_t* thread) {	// bind FBO to bound CBO
	if(*thread->primary > n_objects) return;
	// get bound CBO
	uint64_t cbo_id = thread->bindings.cbo_binding;	// get the ID of the bound CBO
	if(cbo_id == 0) return;	// no CBO is bound
	object_t* object = &objects[cbo_id-1];
	if(object->deleted) return; // bound CBO was previously deleted
	if(*thread->primary == 0)
		object->cbo.bindings[1] = 0;
	else {
		if(*thread->primary > n_objects) return;	// ID specified for object to bind has not been generated
		object_t* bind = &objects[*thread->primary-1];
		if(bind->deleted) return;	// the object specified to bind was previously deleted
		if(bind->privacy_key != thread->privacy_key) return;
		if(bind->type != TYPE_FBO) return;
		object->cbo.bindings[1] = *thread->primary;
	}
}
void instruction_76(thread_t* thread) {	// bind an object to a descriptor
	if(*thread->primary == 0 || *thread->primary > n_objects) return;
	object_t* object = &objects[*thread->primary-1];
	if(object->deleted) return;
	if(object->privacy_key != thread->privacy_key) return;

	uint64_t descriptor_id;	// the ID of the bound descriptor object
	uint32_t level;	// the image level for image descriptors
	if(object->type == TYPE_TBO && *thread->secondary) {
		descriptor_id = thread->bindings.image_desc_binding;	// updating an image descriptor
		level = (*thread->secondary&0xFFFFFFFF)-1;
		if(level > object->tbo.n_levels-1) return;	// level does not exist in the TBO
	} else {
		switch(object->type) {
			case TYPE_TBO: descriptor_id = thread->bindings.sampler_desc_binding; break;	// TBO object; get bound TBO descriptor
			case TYPE_UBO: descriptor_id = thread->bindings.uniform_desc_binding; break;	// UBO object; get bound UBO descriptor
			case TYPE_SBO: descriptor_id = thread->bindings.storage_desc_binding; break;	// SBO object; get bound SBO descriptor
			default: return;
		}
	}

	if(descriptor_id == 0) return;
	object_t* bound_desc = &objects[descriptor_id-1];
	if(bound_desc->deleted) return;	// the descriptor that is bound was deleted at some point
	bound_desc->object_id = *thread->primary;	// set the object the descriptor refers to
	if(object->type == TYPE_TBO && *thread->secondary) bound_desc->image_level = level;
}
void instruction_77(thread_t* thread) {	// bind a pipeline to the bound CBO
	if(*thread->primary > n_objects) return;

	// get bound CBO
	uint64_t cbo_id = thread->bindings.cbo_binding;	// get the ID of the bound CBO
	if(cbo_id == 0) return;	// no CBO is bound
	object_t* bound_cbo = &objects[cbo_id-1];
	if(bound_cbo->deleted) return; // bound CBO was previously deleted

	if(*thread->primary == 0 || *thread->primary > n_objects) return; // ID specified for pipeline to bind has not been generated

	object_t* bind = &objects[*thread->primary-1];
	if(bind->deleted) return;	// the pipeline specified to bind was previously deleted
	if(bind->privacy_key != thread->privacy_key) return;
	switch(bind->type) {
		case TYPE_RASTER_PIPE: // binding a rasterization pipeline
			if(bound_cbo->cbo.pipeline_type == 2 || bound_cbo->cbo.pipeline_type == 0) {
				record_command(&bound_cbo->cbo, 77, thread->primary, 8);	// record the ID of the pipeline to be bound
				bound_cbo->cbo.pipeline_type = 0; 
			}
			break;
		case TYPE_COMPUTE_PIPE: // binding a compute pipeline
			if(bound_cbo->cbo.pipeline_type == 2 || bound_cbo->cbo.pipeline_type == 1) { 
				record_command(&bound_cbo->cbo, 77, thread->primary, 8);	// record the ID of the pipeline to be bound
				bound_cbo->cbo.pipeline_type = 1; 
			} 
			break;
	}
}
void instruction_78(thread_t* thread) { // update a descriptor set
	uint64_t dset_id = thread->bindings.desc_set_binding;
	if(dset_id == 0 || dset_id > n_objects) return;
	object_t* dset_object = &objects[dset_id-1];
	if(dset_object->deleted) return;
	if(objects[dset_object->dset.layout_id-1].deleted) return;	// if the descriptor set layout this descriptor set uses was deleted

	// we don't really need to look at the layout; descriptor sets store all information necessary to update descriptor bindings in this implementation

	desc_set_t* dset = &dset_object->dset;

	uint32_t total_descriptors = 0;
	for(uint32_t i = 0; i < dset->n_bindings+1; i++)
		total_descriptors += dset->bindings[i].n_descs;
	uint32_t n_descriptor_updates = *thread->primary+1;
	if(n_descriptor_updates > total_descriptors) return;

	// make sure all descriptors + bindings exist, and that they're all the correct type
	uint64_t addr = *thread->secondary;
	uint32_t bind_points[n_descriptor_updates];
	uint32_t desc_indices[n_descriptor_updates];
	uint64_t desc_ids[n_descriptor_updates];
	for(uint32_t i = 0; i < n_descriptor_updates; i++) {
		if(check_segfault(thread, addr, 12)) return; // make sure all up to but not including the sampler descriptor index is accessible
		bind_points[i] = read_main_mem_val(thread, addr, 4);
		desc_ids[i] = read_main_mem_val(thread, addr+4, 8);
		desc_indices[i] = 0;
		// make sure the point bind_points[i] exists in the descriptor set and get its type + descriptor count
		uint8_t bind_point_exists = 0;
		uint8_t bind_point_type = 0;
		uint16_t bind_point_n_descs = 0;
		for(uint32_t j = 0; j < dset->n_bindings+1; j++) {
			if(dset->bindings[j].binding_number == bind_points[i]) {
				bind_point_exists = 1;
				bind_point_type = dset->bindings[j].binding_type;
				bind_point_n_descs = dset->bindings[j].n_descs;
				break;
			}
		}
		if(!bind_point_exists) return;
		// make sure object with ID desc_id[i] exists and is of correct type for the binding point
		if(desc_ids[i] == 0 || desc_ids[i] > n_objects) return;
		object_t* desc_object = &objects[desc_ids[i]-1];
		if(desc_object->deleted) return;
		if(desc_object->privacy_key != thread->privacy_key) return;
		if(desc_object->type == TYPE_UNIFORM_DESC && bind_point_type != 0) return;
		if(desc_object->type == TYPE_STORAGE_DESC && bind_point_type != 1) return;
		if(desc_object->type == TYPE_IMAGE_DESC && bind_point_type != 3) return;
		if(desc_object->type == TYPE_SAMPLER_DESC) {
			if(bind_point_type != 2) return;
			if(check_segfault(thread, addr, 14)) return;
			desc_indices[i] = read_main_mem_val(thread, addr+12, 2);
			// make sure the index desc_indices[i] exists for the binding point
			if(desc_indices[i] > bind_point_n_descs) return;
		}
		// make sure there's no repeating binding point number + index pairs
		for(uint32_t j = 0; j < i; j++)
			if(bind_points[i] == bind_points[j] && desc_indices[i] == desc_indices[j])
				return;	// repeated binding point number + index found
		addr += 12;
		if(desc_object->type == TYPE_SAMPLER_DESC) addr += 2;	// take into account the sampler descriptor index
	}

	// update the descriptor set
	for(uint32_t i = 0; i < n_descriptor_updates; i++)
		// all descriptor updates have been validated.
		// bind_points[] describes the binding point for each descriptor update
		// desc_ids[] describes the descriptor object ID for each descriptor update
		// desc_indices[] describes the descriptor index for each descriptor update (0 for non-sampler descriptors)
		for(uint32_t j = 0; j < dset->n_bindings+1; j++)
			if(dset->bindings[j].binding_number == bind_points[i]) {
				object_t* desc_object = &objects[desc_ids[i]-1];
				dset->bindings[j].object_ids[desc_indices[i]] = desc_object->object_id;
				if(desc_object->type == TYPE_SAMPLER_DESC) {
					dset->bindings[j].min_filters[desc_indices[i]] = desc_object->min_filter;
					dset->bindings[j].mag_filters[desc_indices[i]] = desc_object->mag_filter;
					dset->bindings[j].s_modes[desc_indices[i]] = desc_object->s_mode;
					dset->bindings[j].t_modes[desc_indices[i]] = desc_object->t_mode;
				}
			}
}
void instruction_79(thread_t* thread) {	// bind descriptor set, VBO, or IBO to bound command buffer
	if(*thread->primary == 0 || *thread->primary > n_objects) return;

	// get bound CBO
	uint64_t cbo_id = thread->bindings.cbo_binding;
	if(cbo_id == 0) return;
	object_t* bound_cbo = &objects[cbo_id-1];
	if(bound_cbo->deleted) return;
	if(bound_cbo->cbo.pipeline_type == 2) return;	// no pipeline binding command has ever been recorded to this CBO

	// get specified object
	object_t* object = &objects[*thread->primary-1];
	if(object->deleted) return;
	if(object->type != TYPE_DSET && object->type != TYPE_VBO && object->type != TYPE_IBO) return; // object is not a descriptor set, VBO, or IBO
	if(object->privacy_key != thread->privacy_key) return;

	if(object->type == TYPE_DSET) {
		if(*thread->secondary > MAX_NUMBER_BOUND_SETS-1) return;
		if(objects[object->dset.layout_id-1].deleted) return;
		// make sure all descriptor bindings that are in the set are of a type compatible with the CBO's pipeline type
		for(uint32_t i = 0; i < object->dset.n_bindings+1; i++) {
			uint8_t bind_type = object->dset.bindings[i].binding_type;
			if(bound_cbo->cbo.pipeline_type == 0 && bind_type != 0 && bind_type != 2) return;	// rasterization pipeline; uniform + sampler only
			if(bound_cbo->cbo.pipeline_type == 1 && bind_type != 0 && bind_type != 1 && bind_type != 2) return;	// rasterization pipeline; uniform, storage, + sampler only
		}
	}

	uint8_t* info = malloc(9);	// info for command
	*(uint64_t*)info = *thread->primary;	// set the ID of the object to bind
	info[8] = *thread->secondary;	// set the descriptor set binding ID
	record_command(&bound_cbo->cbo, 79, info, 9);	// record the descriptor set binding command into the command buffer
	free(info);
}
void instruction_80(thread_t* thread) {	// outputs the size of a bound buffer
	uint64_t bound_id = 0;

	switch(*thread->primary) {
		case 0: bound_id = thread->bindings.vbo_binding; break;	// VBO
		case 1: bound_id = thread->bindings.ibo_binding; break;	// IBO
		case 2: bound_id = thread->bindings.tbo_binding; break;	// TBO
		case 3: bound_id = thread->bindings.ubo_binding; break;	// UBO
		case 4: bound_id = thread->bindings.sbo_binding; break;	// SBO
		case 7: bound_id = thread->bindings.dbo_binding; break;	// DBO
		case 9: bound_id = thread->bindings.shader_binding; break;	// shader object
		default: if(*thread->primary < 9) *thread->output = 0; return;
	}

	object_t* object = &objects[bound_id-1];
	if(object->deleted) { *thread->output = 0; return; }

	switch(*thread->primary) {
		case 2: *thread->output = 0; return;	// size of TBO
		case 3: *thread->output = object->ubo.size; return;	// UBO
		case 4: *thread->output = object->sbo.size; return;	// SBO
		case 7: *thread->output = object->dbo.size; return; // DBO
		case 9: *thread->output = object->shader.size; return;	// shader object
	}

	GLint gl_size = 0;
	GLenum type = 0;
	switch(*thread->primary) {
		case 0: type = GL_ARRAY_BUFFER; break;
		case 1: type = GL_ELEMENT_ARRAY_BUFFER; break;
	}
	glBindBuffer(type, object->gl_buffer);
	glGetBufferParameteriv(type, GL_BUFFER_SIZE, &gl_size);
	if(gl_size != GL_INVALID_OPERATION) *thread->output = gl_size;
	else *thread->output = 0;
}
void instruction_81(thread_t* thread) {	// maps/unmaps a bound buffer; only updates once unmapped. outputs address to mapped region after a map and updates mapping error bit in SR
	uint64_t bound_id;
	switch(*thread->primary) {
		case 0: bound_id = thread->bindings.vbo_binding; break;	// VBO
		case 1: bound_id = thread->bindings.ibo_binding; break;	// IBO
		case 2: bound_id = thread->bindings.ubo_binding; break;	// UBO
		case 3: bound_id = thread->bindings.sbo_binding; break;	// SBO
		case 4: bound_id = thread->bindings.dbo_binding; break;	// DBO
		case 6: bound_id = thread->bindings.shader_binding; break;	// shader
		default: thread->regs[13] |= 0x20000; return;	// unsupported object (RT object or audio occlusion geometry) or invalid primary register value; do nothing
	}

	if(bound_id == 0) { thread->regs[13] |= 0x20000; return; }	// no object bound
	object_t* object = &objects[bound_id-1];
	if(object->deleted) { thread->regs[13] |= 0x20000; return; }	// the object bound was previously deleted

	// get the size of the buffer for this object
	uint64_t buffer_size = 0;
	GLint gl_size = 0;
	switch(object->type) {
		case TYPE_VBO: glBindBuffer(GL_ARRAY_BUFFER, object->gl_buffer); glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &gl_size); buffer_size = gl_size; break; // get size of VBO
		case TYPE_IBO: glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, object->gl_buffer); glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &gl_size); buffer_size = gl_size; break; // get size of IBO
		case TYPE_UBO: buffer_size = object->ubo.size; break;	// get size of UBO
		case TYPE_SBO: buffer_size = object->sbo.size; break;	// get size of SBO
		case TYPE_DBO: buffer_size = object->dbo.size; break;	// get size of DBO
		case TYPE_VSH: case TYPE_PSH: case TYPE_CSH:
			buffer_size = object->shader.size; break;	// get size of vertex, pixel, or compute shader
	}
	if(object->mapped_address == 0) {	// map this object
		if(buffer_size == 0) { thread->regs[13] |= 0x20000; return; }	// there is no allocated buffer data to map, set buffer map error bit to 1
		object->mapped_address = new_mapping(object->privacy_key, buffer_size);
		// fill memory[object->mapped_address] with buffer data
		switch(*thread->primary) {
			case 0: glBindBuffer(GL_ARRAY_BUFFER, object->gl_buffer); glGetBufferSubData(GL_ARRAY_BUFFER, 0, buffer_size, &memory[object->mapped_address]); break;  // VBO
			case 1: glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, object->gl_buffer); glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, buffer_size, &memory[object->mapped_address]); break;  // IBO
			case 2: memcpy(&memory[object->mapped_address], object->ubo.data, buffer_size); break; // UBO
			case 3: memcpy(&memory[object->mapped_address], object->sbo.data, buffer_size); break; // SBO
			case 4: memcpy(&memory[object->mapped_address], object->dbo.data, buffer_size); break; // DBO
			case 6: memcpy(&memory[object->mapped_address], object->shader.src, buffer_size); break; // shader
			default: thread->regs[13] |= 0x20000; return;
		}
		*thread->output = object->mapped_address;	// output the address of the mapped region to the output register
		thread->regs[13] &= (~0x20000ull); // clear buffer map error bit
	} else {	// unmap this object (update object's store beforehand)
		switch(*thread->primary) {
			case 0: glBindBuffer(GL_ARRAY_BUFFER, object->gl_buffer); glBufferData(GL_ARRAY_BUFFER, buffer_size, &memory[object->mapped_address], GL_DYNAMIC_DRAW); break;  // VBO
			case 1: glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, object->gl_buffer); glBufferData(GL_ELEMENT_ARRAY_BUFFER, buffer_size, &memory[object->mapped_address], GL_DYNAMIC_DRAW); break;  // IBO
			case 2: memcpy(object->ubo.data, &memory[object->mapped_address], buffer_size); break; // UBO
			case 3: memcpy(object->sbo.data, &memory[object->mapped_address], buffer_size); break; // SBO
			case 4: memcpy(object->dbo.data, &memory[object->mapped_address], buffer_size); break;
			case 6: memcpy(object->shader.src, &memory[object->mapped_address], buffer_size); break; // shader
			default: thread->regs[13] |= 0x20000; return;
		}
		thread->regs[13] &= (~0x20000ull); // clear buffer map error bit
		delete_mapping(object->mapped_address);
		object->mapped_address = 0;
	}
}
void instruction_82(thread_t* thread) {	// allocates a buffer
	if(*thread->secondary == 0) { thread->regs[13] |= 0x100; return; }	// specified to allocate 0 bytes; do nothing
	if(*thread->primary == 0 || *thread->primary > n_objects) { thread->regs[13] |= 0x100; return; }	// object specified to allocate for not existing
	object_t* object = &objects[*thread->primary-1];
	if(object->deleted) { thread->regs[13] |= 0x100; return; }		// object has previously been deleted
	if(object->privacy_key != thread->privacy_key) { thread->regs[13] |= 0x100; return; }
	if(object->mapped_address) { thread->regs[13] |= 0x100; return; }	// object is mapped

	switch(object->type) {
		case TYPE_VBO: glBindBuffer(GL_ARRAY_BUFFER, object->gl_buffer); glBufferData(GL_ARRAY_BUFFER, *thread->secondary, 0, GL_DYNAMIC_DRAW); break; // allocate VBO
		case TYPE_IBO: glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, object->gl_buffer); glBufferData(GL_ELEMENT_ARRAY_BUFFER, *thread->secondary, 0, GL_DYNAMIC_DRAW); break; // allocate IBO
		case TYPE_UBO: object->ubo.data = malloc(*thread->secondary); object->ubo.size = *thread->secondary; break;  // allocate UBO
		case TYPE_SBO: object->sbo.data = malloc(*thread->secondary); object->sbo.size = *thread->secondary; break;  // allocate SBO
		case TYPE_DBO: object->dbo.data = malloc(*thread->secondary); object->dbo.size = *thread->secondary; break;  // allocate DBO
		case TYPE_VSH: case TYPE_PSH: case TYPE_CSH:
			object->shader.src = calloc(1,*thread->secondary); object->shader.size = *thread->secondary; break;    // allocate vertex, pixel, or compute shader
		default: thread->regs[13] |= 0x100; return;
	}
	thread->regs[13] &= (~0x100ull); // clear buffer allocation error bit
}
void instruction_83(thread_t* thread) {	// upload to texture
	// upload to bound TBO specified by primary register
	uint64_t bound_id = thread->bindings.tbo_binding;
	if(bound_id == 0 || bound_id > n_objects) return;
	object_t* tbo = &objects[bound_id-1];
	if(tbo->deleted) return;

	if(check_segfault(thread, *thread->primary, 12)) return;

	uint32_t* params = (uint32_t*)read_main_mem(thread, *thread->primary, 12);
	uint32_t width = params[0]+1;
	uint32_t height = params[1]+1;
	uint32_t level = params[2];
	free(params);

	// secondary register is address to texture data
	uint32_t texture_size, bpp;
	switch(tbo->tbo.format) {
		case 0: case 1: case 3: bpp = 1; break;
		case 4: case 5: case 7: bpp = 2; break;
		case 6:  bpp = 8; break;
		case 10: bpp = 16; break;
		default: bpp = 4; break;
	}
	texture_size = bpp*width*height;

	if(check_segfault(thread, *thread->secondary, texture_size)) return;
	uint8_t* tex_data = read_main_mem(thread, *thread->secondary, texture_size);
	upload_texture(&tbo->tbo, level, width, height, tex_data);
	free(tex_data);
}
void instruction_84(thread_t* thread) {	// generate mipmaps for a texture
	uint64_t bound_id = thread->bindings.tbo_binding;
	if(bound_id == 0 || bound_id > n_objects) return;
	object_t* tbo = &objects[bound_id-1];
	if(tbo->deleted) return;
	if(tbo->tbo.format == 12 || tbo->tbo.format == 13) return;	// if depth or depth + stencil texture, do nothing
	if(tbo->tbo.level_widths[0] == 0 && tbo->tbo.level_heights[0] == 0) return;
	if(tbo->tbo.n_levels == 0) return;
	glBindTexture(GL_TEXTURE_2D, tbo->tbo.gl_buffer);
	glGenerateMipmap(GL_TEXTURE_2D);
	uint32_t w = tbo->tbo.level_widths[0];
	uint32_t h = tbo->tbo.level_heights[0];
	for(uint32_t level = 0; !(w == 1 && h == 1); level++) {
		w /= 2;
		h /= 2;
		if(w == 0) w = 1;
		if(h == 0) h = 1;
		if(level == tbo->tbo.n_levels) {
			tbo->tbo.level_widths = realloc(tbo->tbo.level_widths, sizeof(uint32_t)*(tbo->tbo.n_levels+1));
			tbo->tbo.level_heights = realloc(tbo->tbo.level_heights, sizeof(uint32_t)*(tbo->tbo.n_levels+1));
			tbo->tbo.level_widths[level] = w;
			tbo->tbo.level_heights[level] = h;
			tbo->tbo.n_levels++;
		}
	}
}
void instruction_85(thread_t* thread) {	// attach level 0 of bound TBO to bound FBO as attachment
	uint64_t tbo_id = thread->bindings.tbo_binding;
	object_t* tbo = 0;
	if(tbo_id) {
		if(tbo_id > n_objects) return;
		tbo = &objects[tbo_id-1];
		if(tbo->deleted) return;
	}

	uint64_t fbo_id = thread->bindings.fbo_binding;
	if(fbo_id == 0 || fbo_id > n_objects) return;
	object_t* fbo = &objects[fbo_id-1];
	if(fbo->deleted) return;

	glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo.gl_buffer);

	uint8_t any_attachments_bound = 0;
	GLint attached;
	for(uint32_t i = 0; i < 8; i++) {
		glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+i, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attached);
		if(attached != GL_NONE) any_attachments_bound = 1;
	}
	glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attached); if(attached != GL_NONE) any_attachments_bound = 1;
	glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attached); if(attached != GL_NONE) any_attachments_bound = 1;
	uint32_t level = *thread->secondary & 0xFFFFFFFF;
	if(level >= tbo->tbo.n_levels) return;

	if(!any_attachments_bound && tbo_id) {
		fbo->fbo.width = tbo->tbo.level_widths[level];
		fbo->fbo.height = tbo->tbo.level_heights[level];
	} else if(tbo_id) {
		if(fbo->fbo.width != tbo->tbo.level_widths[level]) return;
		if(fbo->fbo.height != tbo->tbo.level_heights[level]) return;
	}

	if(tbo_id == 0) {
		uint32_t attachment = *thread->primary & 0xFFFFFFFF; // attachment to unbind
		if(attachment == 8)    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
		else if(attachment == 9) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
		else if(attachment < 8) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+attachment, GL_TEXTURE_2D, 0, 0);
		else return;
	}
	else if(tbo->tbo.format <= 11) {
		uint32_t attachment = *thread->primary & 0x7;
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+attachment, GL_TEXTURE_2D, tbo->tbo.gl_buffer, level);
	}
	else {
		if(tbo->tbo.format == 12) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tbo->tbo.gl_buffer, level);
		if(tbo->tbo.format == 13) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, tbo->tbo.gl_buffer, level);
	}

	// if there are no attachments anymore, reset fbo's dimensions
	any_attachments_bound = 0;
	for(uint32_t i = 0; i < 8; i++) {
		glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+i, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attached);
		if(attached != GL_NONE) any_attachments_bound = 1;
	}
	glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attached); if(attached != GL_NONE) any_attachments_bound = 1;
	glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attached); if(attached != GL_NONE) any_attachments_bound = 1;
	if(!any_attachments_bound) fbo->fbo.width = 0, fbo->fbo.height = 0;
}
void instruction_86(thread_t* thread) {	// clear buffer(s) of bound FBO
	if(*thread->primary > 10) return;

	// get bound CBO
	uint64_t cbo_id = thread->bindings.cbo_binding;
	if(cbo_id == 0) return;
	object_t* bound_cbo = &objects[cbo_id-1];
	if(bound_cbo->deleted) return;
	if(bound_cbo->cbo.pipeline_type == 2) return;	// no pipeline binding command has ever been recorded to this CBO
	
	uint8_t* info;	// parameters for recorded comamnd
	uint8_t info_length = 1;	// 1 byte for attachment to clear
	
	if(*thread->primary < 9) {
		info_length += 16;
		if(check_segfault(thread, *thread->secondary, 16)) return;
	} else if(*thread->primary == 9) info_length += 4;
	else info_length += 1;

	info = malloc(info_length);
	info[0] = *thread->primary;
	if(*thread->primary < 9) {
		float* data = (float*)read_main_mem(thread, *thread->secondary, 16);
		((float*)(info+1))[0] = *data;
		((float*)(info+1))[1] = data[1];
		((float*)(info+1))[2] = data[2];
		((float*)(info+1))[3] = data[3];
		free(data);
	} else if(*thread->primary == 9) *(float*)(info+1) = *(float*)thread->secondary;	// depth
	else *(info+1) = *thread->secondary & 0xFF;	// stencil

	record_command(&bound_cbo->cbo, 86, info, info_length);
	free(info);
}
void instruction_87(thread_t* thread) { return; }	// unsupported instruction; builds (unsupported) acceleration structure objects
void instruction_88(thread_t* thread) {	// reset the bound CBO
	uint64_t cbo_id = thread->bindings.cbo_binding;
	if(cbo_id == 0) return;
	object_t* bound_cbo = &objects[cbo_id-1];
	if(bound_cbo->deleted) return;

	free(bound_cbo->cbo.cmds);
	bound_cbo->cbo.cmds = 0;
	bound_cbo->cbo.size = 0;
	bound_cbo->cbo.pipeline_type = 2;
}
void instruction_89(thread_t* thread) {	// submit command buffers to graphics queue
	if(check_segfault(thread, *thread->primary, 6)) return;
	uint16_t queue = read_main_mem_val(thread, *thread->primary, 2);
	if(queue > 0) return;	// in this implementation, there is only 1 graphics queue that command buffers can be submitted to
	uint32_t n_cbos = read_main_mem_val(thread, *thread->primary+2, 4) + 1;
	if(check_segfault(thread, *thread->primary, 6+n_cbos*8)) return;
	uint64_t* cbo_ids = (uint64_t*)read_main_mem(thread, *thread->primary + 6, n_cbos*8);
	// make sure all CBO IDs are valid
	for(uint32_t i = 0; i < n_cbos; i++) {
		uint64_t cbo_id = cbo_ids[i];
		if(cbo_id == 0 || cbo_id > n_objects) { free(cbo_ids); return; }
		object_t* cbo = &objects[cbo_id-1];
		if(cbo->deleted) { free(cbo_ids); return; }
		if(cbo->privacy_key != thread->privacy_key) { free(cbo_ids); return; }
		if(cbo->cbo.pipeline_type != 0) { free(cbo_ids); return; }	// command buffer not using rasterization pipelines
	}
	for(uint32_t i = 0; i < n_cbos; i++)
		submit_cmds(&objects[cbo_ids[i]-1].cbo);	// submit this command buffer
	free(cbo_ids);
}
void instruction_90(thread_t* thread) {	// submit command buffers to compute queue
	if(check_segfault(thread, *thread->primary, 6)) return;
	uint16_t queue = read_main_mem_val(thread, *thread->primary, 2);
	if(queue > 0) return;	// in this implementation, there is only 1 compute queue that command buffers can be submitted to
	uint32_t n_cbos = read_main_mem_val(thread, *thread->primary+2, 4) + 1;
	uint64_t* cbo_ids = (uint64_t*)read_main_mem(thread, *thread->primary + 6, n_cbos*8);
	// make sure all CBO IDs are valid
	for(uint32_t i = 0; i < n_cbos; i++) {
		uint64_t cbo_id = cbo_ids[i];
		if(cbo_id == 0 || cbo_id > n_objects) { free(cbo_ids); return; }
		object_t* cbo = &objects[cbo_id-1];
		if(cbo->deleted) { free(cbo_ids); return; }
		if(cbo->privacy_key != thread->privacy_key) { free(cbo_ids); return; }
		if(cbo->cbo.pipeline_type != 1) { free(cbo_ids); return; }	// command buffer not using rasterization pipelines
	}
	for(uint32_t i = 0; i < n_cbos; i++)
		submit_cmds(&objects[cbo_ids[i]-1].cbo);	// submit this command buffer
	free(cbo_ids);
}
void instruction_91(thread_t* thread) { thread->end_cyc = 1; gl_finish = 1; }	// end cycle + wait until all commands have finished 
void instruction_92(thread_t* thread) {	// command for direct draw call
	if(check_segfault(thread, *thread->primary, 13)) return;
	// get bound CBO
	uint64_t cbo_id = thread->bindings.cbo_binding;
	if(cbo_id == 0) return;
	object_t* bound_cbo = &objects[cbo_id-1];
	if(bound_cbo->deleted) return;
	if(bound_cbo->cbo.pipeline_type == 2) return;	// no pipeline binding command has ever been recorded to this CBO
	if(bound_cbo->cbo.pipeline_type != 0) return;	// not a rasterization pipeline CBO

	// is_indexed, n_indices, first_index, n_instances
	uint32_t is_indexed = read_main_mem_val(thread, *thread->primary, 1);
	uint32_t* data = (uint32_t*)read_main_mem(thread, *thread->primary+1, 12);
	uint32_t info[4] = { is_indexed, data[0], data[1], data[2] };	// number of indices to draw, VBO starting index
	free(data);
	record_command(&bound_cbo->cbo, 92, &info, 16);
}
void instruction_93(thread_t* thread) {	// command for indirect draw call
	if(check_segfault(thread, *thread->primary, 21)) return;
	// get bound CBO
	uint64_t cbo_id = thread->bindings.cbo_binding;
	if(cbo_id == 0) return;
	object_t* bound_cbo = &objects[cbo_id-1];
	if(bound_cbo->deleted) return;
	if(bound_cbo->cbo.pipeline_type == 2) return;	// no pipeline binding command has ever been recorded to this CBO
	if(bound_cbo->cbo.pipeline_type != 0) return;	// not a rasterization pipeline CBO 

	// is_indexed, data buffer ID, offset, n_calls
	uint8_t is_indexed = read_main_mem_val(thread, *thread->primary, 1);
	uint64_t* data = (uint64_t*)read_main_mem(thread, *thread->primary+1, 16);
	uint32_t n_draws = read_main_mem_val(thread, *thread->primary+17, 4);
	uint64_t info[4] = { is_indexed, data[0], data[1], n_draws };
	free(data);
	if(info[2] % 4) return; // offset into data buffer must be a multiple of 4
	if(info[1] == 0 || info[1] > n_objects || objects[info[1]-1].type != TYPE_DBO) return;
	if(objects[info[1]-1].privacy_key != thread->privacy_key) return;
	if(objects[info[1]-1].deleted) return;	// data buffer object had been deleted
	record_command(&bound_cbo->cbo, 93, &info, 32);
}
void instruction_94(thread_t* thread) {	// command to update data buffer store
	if(check_segfault(thread, *thread->primary, 18)) return;
	// get bound CBO
	uint64_t cbo_id = thread->bindings.cbo_binding;
	if(cbo_id == 0) return;
	object_t* bound_cbo = &objects[cbo_id-1];
	if(bound_cbo->deleted) return;
	if(bound_cbo->cbo.pipeline_type == 2) return;	// no pipeline binding command has ever been recorded to this CBO

	uint64_t* data = (uint64_t*)read_main_mem(thread, *thread->primary, 16);
	uint16_t n_bytes = read_main_mem_val(thread, *thread->primary+16, 2);
	uint64_t* info = malloc(24+n_bytes);
	info[0] = data[0];	// data buffer ID
	info[1] = data[1];	// data buffer offset
	info[2] = n_bytes;	// n_bytes
	free(data);
	if(check_segfault(thread, *thread->primary+18, n_bytes)) { free(info); return; }
	data = (uint64_t*)read_main_mem(thread, *thread->primary+18, n_bytes);
	memcpy(&info[3], data, n_bytes);

	if(info[0] == 0 || info[0] > n_objects || objects[info[0]-1].type != TYPE_DBO || objects[info[0]-1].deleted) { free(info); return; }
	if(objects[info[0]-1].privacy_key != thread->privacy_key) { free(info); return; }
	if(info[1] % 4 || (info[2]+1) % 4) { free(info); return; }	// offset + # bytes must be mult of 4
	record_command(&bound_cbo->cbo, 94, &info, 24+n_bytes);
	free(info);
}
void instruction_95(thread_t* thread) {	// command to update push constants
	if(check_segfault(thread, *thread->primary, 17)) return;
	// get bound CBO
	uint64_t cbo_id = thread->bindings.cbo_binding;
	if(cbo_id == 0) return;
	object_t* bound_cbo = &objects[cbo_id-1];
	if(bound_cbo->deleted) return;
	if(bound_cbo->cbo.pipeline_type == 2) return;	// no pipeline binding command has ever been recorded to this CBO

	uint64_t* data = (uint64_t*)read_main_mem(thread, *thread->primary, 16);
	uint8_t n_bytes = read_main_mem_val(thread, *thread->primary+16, 1);
	uint64_t info[3] = { data[0], data[1], n_bytes };
	free(data);

	if(info[0] == 0 || info[0] > n_objects || objects[info[0]-1].type != TYPE_DBO || objects[info[0]-1].deleted) return;
	if(objects[info[0]-1].privacy_key != thread->privacy_key) return;
	if(info[1] % 4 || (info[2]+1) % 4) return;	// offset + # bytes must be mult of 4
	record_command(&bound_cbo->cbo, 95, &info, 24);
}
void instruction_96(thread_t* thread) { return; }	// unsupported instruction; trace rays
void instruction_97(thread_t* thread) { return; }	// unsupported instruction; copy acceleration structure
void instruction_98(thread_t* thread) { gl_swap = 1; }	// swap buffers
void instruction_99(thread_t* thread) {	// set current display
	if((*thread->primary & 0xFF) > 0) return;	// only one display is supported in this implementation
	thread->regs[13] &= (~0xFFull);	// clear the display number (since display 0 is the only supported display)
}
void instruction_100(thread_t* thread) {	// set filter/wrapping properties for bound sampler descriptor
	uint64_t tbo_desc_id = thread->bindings.sampler_desc_binding;
	if(tbo_desc_id == 0) return;
	object_t* tbo_desc = &objects[tbo_desc_id-1];
	if(tbo_desc->deleted) return;
	switch(*thread->primary & 0x3) {
		case 0: if(*thread->secondary > 5) return; tbo_desc->min_filter = *thread->secondary; break;
		case 1: if(*thread->secondary > 1) return; tbo_desc->mag_filter = *thread->secondary; break;
		case 2: if(*thread->secondary > 2) return; tbo_desc->s_mode = *thread->secondary; break;
		case 3: if(*thread->secondary > 2) return; tbo_desc->t_mode = *thread->secondary; break;
	}
}
void instruction_101(thread_t* thread) {	// compute dispatch
	if(check_segfault(thread, *thread->primary, 12)) return; // accessing the specified X, Y, Z parameters for work-group dimensions segfaults

	// get bound CBO
	uint64_t cbo_id = thread->bindings.cbo_binding;
	if(cbo_id == 0) return;
	object_t* bound_cbo = &objects[cbo_id-1];
	if(bound_cbo->deleted) return;
	if(bound_cbo->cbo.pipeline_type == 2) return;	// no pipeline binding command has ever been recorded to this CBO
	if(bound_cbo->cbo.pipeline_type != 1) return;	// not a compute pipeline CBO

	uint32_t* params = (uint32_t*)read_main_mem(thread, *thread->primary, 12);

	if(!params[0] || !params[1] || !params[2]) return;

	uint32_t* global_work = (uint32_t*)(memory + HW_INFORMATION + 167);
	if(params[0] > global_work[0] || params[1] > global_work[1] || params[2] > global_work[2] || params[0]*params[1]*params[2] > global_work[3])
		return; // if dimensions exceed their maximum or the product exceeds the maximum global work-group size, do nothing

	uint32_t info[3] = { params[0], params[1], params[2] };      // info for command
	free(params);
	record_command(&bound_cbo->cbo, 101, info, 12);   // record the dispatch compute command into the command buffer
}
void instruction_102(thread_t* thread) { *thread->output = HW_INFORMATION; }
void instruction_103(thread_t* thread) {
	uint64_t segtable_id = thread->bindings.segtable_binding;
	if(segtable_id == 0 || segtable_id == thread->segtable_id) return;	// no segtable is bound or it's the segtable this thread is using
	object_t* object = &objects[segtable_id-1];
	if(object->deleted) return; // bound segtable was previously deleted
	segtable_t* segtable = &object->segtable;

	segment_t default_segment;
	default_segment.v_address = 0;		default_segment.p_address = 0;
	default_segment.length = 0;			default_segment.deleted = 0;

	if(*thread->primary == 0) *thread->output = add_segment(segtable, default_segment);
	else if(*thread->primary == 1) {
		if(check_segfault(thread, *thread->secondary, 32)) return;
		uint64_t* args = (uint64_t*)read_main_mem(thread, *thread->secondary, 32);
		if(args[3] >= segtable->n_segments) return;
		segment_t* segment = &segtable->segments[args[3]];
		if(segment->deleted) return;
		if(args[2] == 0) return;
		if(args[0] + args[2] >= SIZE_MAIN_MEM) return;
		if(args[1] + args[2] >= SIZE_MAIN_MEM) return;
		segment->v_address = args[0];
		segment->p_address = args[1];
		segment->length = args[2];
	} else if(*thread->primary == 6) *thread->output = segtable->n_segments;
	else if(*thread->primary == 7) reset_segtable(segtable);
	else {
		if(*thread->secondary >= segtable->n_segments) return;
		segment_t* segment = &segtable->segments[*thread->secondary];
		switch(*thread->primary) {
			case 2: segment->deleted = 1; break;
			case 3: *thread->output = segment->v_address; break;
			case 4: *thread->output = segment->p_address; break;
			case 5: *thread->output = segment->length; break;
			case 8: *thread->output = segment->deleted; break;
			case 9: *segment = default_segment; break;
		}
	}
}
void instruction_104(thread_t* thread) { *thread->primary &= 0xFF; }
void instruction_105(thread_t* thread) { *thread->primary &= 0xFFFF; }
void instruction_106(thread_t* thread) { *thread->primary &= 0xFFFFFFFF; }
void instruction_107(thread_t* thread) {
	if(*thread->primary & 0x80) *thread->primary |= 0xFFFFFFFFFFFFFF00;
	else *thread->primary &= 0xFF;
}
void instruction_108(thread_t* thread) {
	if(*thread->primary & 0x8000) *thread->primary |= 0xFFFFFFFFFFFF0000;
	else *thread->primary &= 0xFFFF;
}
void instruction_109(thread_t* thread) {
	if(*thread->primary & 0x80000000) *thread->primary |= 0xFFFFFFFF00000000;
	else *thread->primary &= 0xFFFFFFFF;
}
void instruction_110(thread_t* thread) { *thread->primary = ~(*thread->primary); }
void instruction_111(thread_t* thread) {
	*thread->primary = ~(*thread->primary);
	*thread->primary ^= 0x80000000;
	*thread->primary = ~(*thread->primary);
}
void instruction_112(thread_t* thread) {
	*thread->primary = ~(*thread->primary);
	*thread->primary ^= 0x8000000000000000;
	*thread->primary = ~(*thread->primary);
}
void instruction_113(thread_t* thread) { (*thread->primary)++; } // 64-bit int increment primary register
void instruction_114(thread_t* thread) { (*thread->primary)--; } // 64-bit int decrement primary register
void instruction_115(thread_t* thread) { // float32 primary % secondary register
	float result;
	uint32_t a = *thread->primary & 0xFFFFFFFF;
	uint32_t b = *thread->secondary & 0xFFFFFFFF;
	float x = *(float*)&a;
	float y = *(float*)&b;
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y) || x < 0.0 || y <= 0.0) result = NAN;
	else result = fmodf(x,y);        // x is finite && != NAN, y is finite && != NAN && != 0
	*thread->output = *(uint32_t*)&result;
}
void instruction_116(thread_t* thread) { // float64 primary % secondary register
	double result;
	double x = *(double*)thread->primary;
	double y = *(double*)thread->secondary;
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y) || x < 0.0 || y <= 0.0) result = NAN;
	else result = fmod(x,y);	// x is finite && != NAN, y is finite && != NAN && != 0
	*thread->output = *(uint64_t*)&result;
}
void instruction_117(thread_t* thread) { // single variable math function
	double fval = *(double*)thread->secondary;
	int64_t ival = *(int64_t*)thread->secondary;
	uint64_t result;
	switch(*thread->primary) {
		case 0: fval = tan(fval); break;
		case 1: fval = sin(fval); break;
		case 2: fval = cos(fval); break;
		case 3: fval = atan(fval); break;
		case 4: fval = asin(fval); break;
		case 5: fval = acos(fval); break;
		case 6: fval = tanh(fval); break;
		case 7: fval = sinh(fval); break;
		case 8: fval = cosh(fval); break;
		case 9: fval = atanh(fval); break;
		case 10: fval = asinh(fval); break;
		case 11: fval = acosh(fval); break;
		case 12: fval = log(fval); break;
		case 13: fval = log10(fval); break;
		case 14: fval = floor(fval); break;
		case 15: fval = ceil(fval); break;
		case 16: fval = fabs(fval); break;
		case 17: ival = abs64(ival); break;
		default: return;
	}
	if(*thread->primary < 17) result = *(uint64_t*)&fval;
	else result = ival;
	*thread->output = result;
}
void instruction_118(thread_t* thread) { // copy display/camera contents to memory
	if(*thread->primary != 0) return;	// only capturing from display is supported
	if(thread->perm_screenshot) return;	// do not have necessary permission to capture from display
	uint64_t address = *(uint64_t*)(memory + HW_INFORMATION + 1);
	uint32_t* dimensions = (uint32_t*)&memory[address];
	uint32_t width = dimensions[0];  // display width
	uint32_t height = dimensions[1]; // display height
	if(check_segfault(thread, *thread->secondary, width*height*4)) return;
}
void instruction_119(thread_t* thread) { // output time information
	struct timespec tm;
	time_t current;
	time(&current);
	struct tm utc = *gmtime(&current);
	switch(*thread->primary) {	// get time data
		case 0: clock_gettime(CLOCK_REALTIME, &tm); *thread->output = (tm.tv_sec-start_tm.tv_sec)*NS_PER_SEC + (tm.tv_nsec-start_tm.tv_nsec); break;
		case 1: *thread->output = utc.tm_sec; break;
		case 2: *thread->output = utc.tm_min; break;
		case 3: *thread->output = utc.tm_hour; break;
		case 4: *thread->output = utc.tm_mday; break;
		case 5: *thread->output = utc.tm_mon; break;
		case 6: *thread->output = utc.tm_year + 1900; break;
		case 7: *thread->output = utc.tm_wday; break;
		case 8: *thread->output = utc.tm_yday; break;
		case 9: *thread->output = utc.tm_isdst; break;
		case 10: *thread->output = utc.tm_sec * 1000; break;	// UTC number of milliseconds since midnight; inaccurate for now
	}
}
void instruction_120(thread_t* thread) { // copy region of memory to another location
	if(*thread->secondary == 0) return;	// # of bytes is 0
	uint8_t* read_data = 0;

	if(*thread->primary >= mappings_low && *thread->primary < HW_INFORMATION) { // read is within the area for mapped buffer regions
		if(check_mapped_region(thread->privacy_key, *thread->primary, *thread->secondary) == 0) return; // read is not within a mapped buffer region
		read_data = malloc(*thread->secondary);
		memcpy(read_data, memory+(*thread->primary), *thread->secondary);
	} else if(check_segfault(thread, *thread->primary, *thread->secondary)) return; // segfault; not all addresses in range both accessible & within main memory
	else read_data = read_main_mem(thread, *thread->primary, *thread->secondary);

	if(*thread->output >= mappings_low && *thread->output < HW_INFORMATION) { // write is within the area for mapped buffer regions
		if(check_mapped_region(thread->privacy_key, *thread->output, *thread->secondary))
			memcpy(memory+(*thread->output), read_data, *thread->secondary);
	} else if(!check_segfault(thread, *thread->output, *thread->secondary))
		write_main_mem(thread, *thread->output, read_data, *thread->secondary);
	free(read_data);
}
void instruction_121(thread_t* thread) { return; }	// configure/get info from audio sources + listeners
void instruction_122(thread_t* thread) { return; }	// get/set info related to audio data/files
void instruction_123(thread_t* thread) {	// get/set info related to video data/files
	if(!thread->perm_file_io || !thread->bindings.vid_data_binding)
		return;

	object_t* vid_object = &objects[thread->bindings.vid_data_binding-1];
	if(vid_object->deleted) return;	// the FBO bound to the command buffer being submitted has previously been deleted
	vid_data_t* vid_data = &vid_object->vid_data;

	// load video/image data from file to vid_data
	if(*thread->primary == 0) {		// load video/image data from file to vid_data
		if(check_segfault(thread, *thread->secondary, 16)) return;
		uint32_t first_frame = read_main_mem_val(thread, *thread->secondary, 4);
		uint32_t last_frame = read_main_mem_val(thread, *thread->secondary+4, 4);
		if(first_frame != 0 || last_frame != 0) return;	// currently only support image files (1 frame)

		uint64_t path_addr = read_main_mem_val(thread, *thread->secondary+8, 8);
		uint8_t* path_str = 0;
		uint32_t path_length = get_string_main_mem(thread, path_addr, &path_str);
		if(!path_length || !validate_path(path_str) || check_path_existence(path_str) != 1) return;
		if(check_highest_path(thread->highest_dir, path_str)) return;
		char* ext = get_string_file_ext(path_str);
		if(strcmp(ext, "png") != 0 && strcmp(ext, "jpg") != 0 && strcmp(ext, "jpeg") != 0) return;

		uint8_t* full_path;
		uint32_t full_size;
		get_full_path(path_str, &full_path, &full_size);

		// load the image data into vid_data
		uint32_t w, h, n;
		uint8_t* frame = stbi_load(full_path, &w, &h, &n, 4);
		if(!frame) return;			// image file is invalid

		*thread->output = (n == 4);

		for(uint32_t i = 0; i < vid_data->n_frames; i++)
			free(vid_data->frames[i]);
		if(vid_data->n_frames)
			free(vid_data->frames);

		vid_data->frames = malloc(sizeof(uint8_t*));
		vid_data->frames[0] = malloc(w*h*4);
		memcpy(vid_data->frames[0], frame, w*h*4);
		vid_data->n_frames = 1;
		vid_data->width = w;
		vid_data->height = h;

		stbi_image_free(frame);
	} else if(*thread->primary == 1) {		// get the frame count of a file
		uint8_t* path_str = 0;
		uint32_t path_length = get_string_main_mem(thread, *thread->secondary, &path_str);
		if(!path_length || !validate_path(path_str) || check_path_existence(path_str) != 1) return;
		if(check_highest_path(thread->highest_dir, path_str)) return;
		char* ext = get_string_file_ext(path_str);
		if(strcmp(ext, "png") != 0 && strcmp(ext, "jpg") != 0 && strcmp(ext, "jpeg") != 0) return;

		uint8_t* full_path;
		uint32_t full_size;
		get_full_path(path_str, &full_path, &full_size);

		// load the image to check if it's valid
		uint32_t w, h, n;
		uint8_t* frame = stbi_load(full_path, &w, &h, &n, 4);
		if(!frame) return;			// image file is invalid
		stbi_image_free(frame);

		*thread->output = 1;		// if image is valid, it contains 1 frame
	} else if(*thread->primary == 2)		// get frame count of vid_data
		*thread->output = vid_data->n_frames;
	else if(*thread->primary == 3) {		// get resolution of a file
		if(check_segfault(thread, *thread->secondary, 16)) return;
		uint64_t path_addr = read_main_mem_val(thread, *thread->secondary+8, 8);
		uint8_t* path_str = 0;
		uint32_t path_length = get_string_main_mem(thread, path_addr, &path_str);
		if(!path_length || !validate_path(path_str) || check_path_existence(path_str) != 1) return;
		if(check_highest_path(thread->highest_dir, path_str)) return;
		char* ext = get_string_file_ext(path_str);
		if(strcmp(ext, "png") != 0 && strcmp(ext, "jpg") != 0 && strcmp(ext, "jpeg") != 0) return;

		uint8_t* full_path;
		uint32_t full_size;
		get_full_path(path_str, &full_path, &full_size);

		// load image to obtain its resolution
		uint32_t w, h, n;
		uint8_t* frame = stbi_load(full_path, &w, &h, &n, 4);
		if(!frame) return;			// image file is invalid
		stbi_image_free(frame);

		uint32_t data[] = { w,h };
		write_main_mem(thread, *thread->secondary, (uint8_t*)data, 8);
	} else if(*thread->primary == 4) {		// get resolution of vid_data
		if(check_segfault(thread, *thread->secondary, 8)) return;
		uint32_t data[] = { vid_data->width, vid_data->height };
		write_main_mem(thread, *thread->secondary, (uint8_t*)data, 8);
	} else if(*thread->primary == 5)		// get frame rate of vid_data
		*thread->output = 0;	// images have a frame rate of 0
	else if(*thread->primary == 6) {		// get frame rate of a file
		uint8_t* path_str = 0;
		uint32_t path_length = get_string_main_mem(thread, *thread->secondary, &path_str);
		if(!path_length || !validate_path(path_str) || check_path_existence(path_str) != 1) return;
		if(check_highest_path(thread->highest_dir, path_str)) return;
		char* ext = get_string_file_ext(path_str);
		if(strcmp(ext, "png") != 0 && strcmp(ext, "jpg") != 0 && strcmp(ext, "jpeg") != 0) return;

		uint8_t* full_path;
		uint32_t full_size;
		get_full_path(path_str, &full_path, &full_size);

		// load the image to check if it's valid
		uint32_t w, h, n;
		uint8_t* frame = stbi_load(full_path, &w, &h, &n, 4);
		if(!frame) return;			// image file is invalid
		stbi_image_free(frame);

		*thread->output = 0;		// if image is valid, it has frame rate of 0
	} else if(*thread->primary == 7) {		// copy a frame from vid_data to memory
		if(check_segfault(thread, *thread->secondary, 12)) return;
		uint32_t frame = read_main_mem_val(thread, *thread->secondary, 4);
		if(frame >= vid_data->n_frames) return;
		uint64_t output_addr = read_main_mem_val(thread, *thread->secondary+4, 8);
		uint32_t frame_size = vid_data->width * vid_data->height * 4;
		if(check_segfault(thread, output_addr, frame_size)) return;
		write_main_mem(thread, output_addr, vid_data->frames[frame], frame_size);
	} else if(*thread->primary == 8) {		// overwrite a frame of a file
		if(check_segfault(thread, *thread->secondary, 20)) return;
		uint32_t frame_num = read_main_mem_val(thread, *thread->secondary, 4);
		uint64_t data_addr = read_main_mem_val(thread, *thread->secondary+4, 8);
		if(frame_num > 0) return;	// currently only support image files (1 frame)

		uint64_t path_addr = read_main_mem_val(thread, *thread->secondary+12, 8);
		uint8_t* path_str = 0;
		uint32_t path_length = get_string_main_mem(thread, path_addr, &path_str);
		if(!path_length || !validate_path(path_str) || check_path_existence(path_str) != 1) return;
		if(check_highest_path(thread->highest_dir, path_str)) return;
		char* ext = get_string_file_ext(path_str);
		if(strcmp(ext, "png") != 0 && strcmp(ext, "jpg") != 0 && strcmp(ext, "jpeg") != 0) return;

		uint8_t* full_path;
		uint32_t full_size;
		get_full_path(path_str, &full_path, &full_size);

		// load the image to check if it's valid
		uint32_t w, h, n;
		uint8_t* img = stbi_load(full_path, &w, &h, &n, 4);
		if(!img) return;			// image file is invalid
		stbi_image_free(img);

		if(check_segfault(thread, data_addr, w*h*4)) return;
		uint8_t* data = read_main_mem(thread, data_addr, w*h*4);

		if(strcmp(ext, "png") == 0)
			stbi_write_png(full_path, w, h, 4, data, w*4);
		else {
			uint8_t* jpg_data = malloc(w*h*3);			// must discard the 4th byte of each pixel in 'data' (only have 3 components)
			for(uint32_t i = 0; i < w*h*4; i++)
				if(i % 4 != 3)		// skip alpha byte
					jpg_data[i-i/4] = data[i];
			stbi_write_jpg(full_path, w, h, 3, jpg_data, 100);
			free(jpg_data);
		}
		free(data);
	} else if(*thread->primary == 9) {		// set frame count/rate of a file
		if(check_segfault(thread, *thread->secondary, 24)) return;
		uint32_t frame_rate = read_main_mem_val(thread, *thread->secondary, 4);
		uint32_t frame_count = read_main_mem_val(thread, *thread->secondary+4, 4);
		uint32_t frame_width = read_main_mem_val(thread, *thread->secondary+8, 4);
		uint32_t frame_height = read_main_mem_val(thread, *thread->secondary+12, 4);
		if(frame_count != 1 || frame_rate != 0) return;		// currently only support image files
		if(frame_width == 0 || frame_height == 0) return;

		uint64_t path_addr = read_main_mem_val(thread, *thread->secondary+16, 8);
		uint8_t* path_str = 0;
		uint32_t path_length = get_string_main_mem(thread, path_addr, &path_str);
		if(!path_length || !validate_path(path_str) || check_path_existence(path_str) != 1) return;
		if(check_highest_path(thread->highest_dir, path_str)) return;
		char* ext = get_string_file_ext(path_str);
		if(strcmp(ext, "png") != 0 && strcmp(ext, "jpg") != 0 && strcmp(ext, "jpeg") != 0) return;


		uint8_t* full_path;
		uint32_t full_size;
		get_full_path(path_str, &full_path, &full_size);

		// load the image to check if it's valid
		uint32_t w, h, n;
		uint8_t* img = stbi_load(full_path, &w, &h, &n, 4);
		if(!img) {			// image file is invalid; overwrite file content with a blank image (stbi_write_*)
			uint8_t* data = calloc(1,frame_width*frame_height*4);
			if(strcmp(ext, "png") == 0)
				stbi_write_png(full_path, frame_width, frame_height, 4, data, frame_width*4);
			else
				stbi_write_jpg(full_path, frame_width, frame_height, 3, data, 100);
			free(data);
		} else
			stbi_image_free(img);
	}
}
void instruction_124(thread_t* thread) { return; }	// networking
void instruction_125(thread_t* thread) { return; }	// load to left SIMD vector
void instruction_126(thread_t* thread) { return; }	// load to right SIMD vector
void instruction_127(thread_t* thread) { return; }	// SIMD
void instruction_128(thread_t* thread) { // 32-bit integer addition
	*thread->output = (uint32_t)(*thread->primary&0xFFFFFFFF) + (uint32_t)(*thread->secondary&0xFFFFFFFF);
	*thread->output &= 0xFFFFFFFF;
}
void instruction_129(thread_t* thread) { // 32-bit integer subtraction
	*thread->output = (uint32_t)(*thread->primary&0xFFFFFFFF) - (uint32_t)(*thread->secondary&0xFFFFFFFF);
	*thread->output &= 0xFFFFFFFF;
}
void instruction_130(thread_t* thread) { // 32-bit integer multiply
	*thread->output = (uint32_t)(*thread->primary&0xFFFFFFFF) * (uint32_t)(*thread->secondary&0xFFFFFFFF);
	*thread->output &= 0xFFFFFFFF;
}
void instruction_131(thread_t* thread) { // 32-bit signed integer division
	if((*thread->secondary&0xFFFFFFFF) == 0) *thread->output = 0;
	else *thread->output = (int32_t)(*(int64_t*)thread->primary&0xFFFFFFFF) / (int32_t)(*(int64_t*)thread->secondary&0xFFFFFFFF);
	*thread->output &= 0xFFFFFFFF;
}
void instruction_132(thread_t* thread) { // 32-bit unsigned integer division
	if((*thread->secondary&0xFFFFFFFF) == 0) *thread->output = 0;
	else *thread->output = (uint32_t)(*thread->primary&0xFFFFFFFF) / (uint32_t)(*thread->secondary&0xFFFFFFFF);
	*thread->output &= 0xFFFFFFFF;
}
void instruction_133(thread_t* thread) { // 32-bit signed integer modulo
	if((*thread->secondary&0xFFFFFFFF) == 0) *thread->output = 0;
	else *thread->output = (int32_t)(*thread->primary&0xFFFFFFFF) % (int32_t)(*thread->secondary&0xFFFFFFFF);
	*thread->output &= 0xFFFFFFFF;
}
void instruction_134(thread_t* thread) { // 64-bit integer addition
	*thread->output = *thread->primary + *thread->secondary;
}
void instruction_135(thread_t* thread) { // 64-bit integer subtraction
	*thread->output = *thread->primary - *thread->secondary;
}
void instruction_136(thread_t* thread) { // 64-bit integer multiply
	*thread->output = *thread->primary * *thread->secondary;
}
void instruction_137(thread_t* thread) { // 64-bit signed integer division
	if(*thread->secondary == 0) *thread->output = 0;
	else *thread->output = *(int64_t*)thread->primary / *(int64_t*)thread->secondary;
}
void instruction_138(thread_t* thread) { // 64-bit unsigned integer division
	if(*thread->secondary == 0) *thread->output = 0;
	else *thread->output = *thread->primary / *thread->secondary;
}
void instruction_139(thread_t* thread) { // 64-bit signed integer modulo
	if(*thread->secondary == 0) *thread->output = 0;
	else *thread->output = *(int64_t*)thread->primary % *(int64_t*)thread->secondary;
}
void instruction_140(thread_t* thread) { // 32-bit float addition
	uint32_t a = *thread->primary & 0xFFFFFFFF;
	uint32_t b = *thread->secondary & 0xFFFFFFFF;
	float x = *(float*)&a;
	float y = *(float*)&b;
	float res = x+y;
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y)) res = NAN;
	*thread->output = *(uint32_t*)&res;
}
void instruction_141(thread_t* thread) { // 32-bit float subtraction
	uint32_t a = *thread->primary & 0xFFFFFFFF;
	uint32_t b = *thread->secondary & 0xFFFFFFFF;
	float x = *(float*)&a;
	float y = *(float*)&b;
	float res = x-y;
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y)) res = NAN;
	*thread->output = *(uint32_t*)&res;
}
void instruction_142(thread_t* thread) { // 32-bit float multiply
	uint32_t a = *thread->primary & 0xFFFFFFFF;
	uint32_t b = *thread->secondary & 0xFFFFFFFF;
	float x = *(float*)&a;
	float y = *(float*)&b;
	float res = x*y;
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y)) res = NAN;
	*thread->output = *(uint32_t*)&res;
}
void instruction_143(thread_t* thread) { // 32-bit float division
	uint32_t a = *thread->primary & 0xFFFFFFFF;
	uint32_t b = *thread->secondary & 0xFFFFFFFF;
	float x = *(float*)&a;
	float y = *(float*)&b;
	float res;
	if(y != 0.0f) res = x/y;
	else res = 0.0f;
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y)) res = NAN;
	*thread->output = *(uint32_t*)&res;
}
void instruction_144(thread_t* thread) { // 32-bit float raise to power
	uint32_t a = *thread->primary & 0xFFFFFFFF;
	uint32_t b = *thread->secondary & 0xFFFFFFFF;
	float x = *(float*)&a;
	float y = *(float*)&b;
	float res = powf(x,y);
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y)) res = NAN;
	*thread->output = *(uint32_t*)&res;
}
void instruction_145(thread_t* thread) { // 64-bit float addition
	double x = *(double*)thread->primary;
	double y = *(double*)thread->secondary;
	double res = x+y;
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y)) res = NAN;
	*thread->output = *(uint64_t*)&res;
}
void instruction_146(thread_t* thread) { // 64-bit float subtraction
	double x = *(double*)thread->primary;
	double y = *(double*)thread->secondary;
	double res = x-y;
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y)) res = NAN;
	*thread->output = *(uint64_t*)&res;
}
void instruction_147(thread_t* thread) { // 64-bit float multiply
	double x = *(double*)thread->primary;
	double y = *(double*)thread->secondary;
	double res = x*y;
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y)) res = NAN;
	*thread->output = *(uint64_t*)&res;
}
void instruction_148(thread_t* thread) { // 64-bit float division
	double x = *(double*)thread->primary;
	double y = *(double*)thread->secondary;
	double res;
	if(y != 0.0) res = x/y;
	else res = 0.0;
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y)) res = NAN;
	*thread->output = *(uint64_t*)&res;
}
void instruction_149(thread_t* thread) { // 64-bit float raise to power
	double x = *(double*)thread->primary;
	double y = *(double*)thread->secondary;
	double res = pow(x,y);
	if(isnan(x) || isnan(y) || isinf(x) || isinf(y)) res = NAN;
	*thread->output = *(uint64_t*)&res;
}
void instruction_150(thread_t* thread) { // 32-bit integer compare
	thread->regs[13] &= ~(SR_BIT_V|SR_BIT_C|SR_BIT_Z|SR_BIT_N); // clear all conditional bits
	if(check_overflow32(*(int32_t*)thread->primary, -*(int32_t*)thread->secondary)) thread->regs[13] |= SR_BIT_V; // set V
	if((*thread->primary&0xFFFFFFFF) >= (*thread->secondary&0xFFFFFFFF)) thread->regs[13] |= SR_BIT_C; // set C
	if(*(int32_t*)thread->primary < *(int32_t*)thread->secondary) thread->regs[13] |= SR_BIT_N; // set N
	if((*thread->primary&0xFFFFFFFF) == (*thread->secondary&0xFFFFFFFF)) thread->regs[13] |= SR_BIT_Z; // set Z
}
void instruction_151(thread_t* thread) { // 64-bit integer compare
	thread->regs[13] &= ~(SR_BIT_V|SR_BIT_C|SR_BIT_Z|SR_BIT_N); // clear all conditional bits
	if(check_overflow64(*thread->primary, -*thread->secondary)) thread->regs[13] |= SR_BIT_V; // set V
	if(*thread->primary >= *thread->secondary) thread->regs[13] |= SR_BIT_C; // set C
	if(*(int64_t*)thread->primary < *(int64_t*)thread->secondary) thread->regs[13] |= SR_BIT_N; // set N
	if(*thread->primary == *thread->secondary) thread->regs[13] |= SR_BIT_Z; // set Z
}
void instruction_152(thread_t* thread) { // 32-bit float compare
	thread->regs[13] &= ~(SR_BIT_V|SR_BIT_C|SR_BIT_Z|SR_BIT_N); // clear all conditional bits
	uint32_t a = *thread->primary & 0xFFFFFFFF;
	uint32_t b = *thread->secondary & 0xFFFFFFFF;
	float x = *(float*)&a;
	float y = *(float*)&b;
	if(isnan(x) || isnan(y)) {
		thread->regs[13] |= SR_BIT_C;
		thread->regs[13] |= SR_BIT_V;
		return;
	}
	if(x == y) thread->regs[13] |= SR_BIT_Z;
	if(x >= y) thread->regs[13] |= SR_BIT_C;
	if(x < y) thread->regs[13] |= SR_BIT_N;
}
void instruction_153(thread_t* thread) { // 64-bit float compare
	thread->regs[13] &= ~(SR_BIT_V|SR_BIT_C|SR_BIT_Z|SR_BIT_N); // clear all conditional bits
	double x = *(double*)thread->primary;
	double y = *(double*)thread->secondary;
	if(isnan(x) || isnan(y)) {
		thread->regs[13] |= SR_BIT_C;
		thread->regs[13] |= SR_BIT_V;
		return;
	}
	if(x == y) thread->regs[13] |= SR_BIT_Z;
	if(x >= y) thread->regs[13] |= SR_BIT_C;
	if(x < y) thread->regs[13] |= SR_BIT_N;
}
void instruction_154(thread_t* thread) {
	// 32-bit float to 64-bit float
	uint32_t x = *thread->primary & 0xFFFFFFFF;
	float y = *(float*)&x;
	double z = y;
	*thread->output = *(uint64_t*)&z;
}
void instruction_155(thread_t* thread) {
	// 64-bit float to 32-bit float
	uint64_t x = *thread->primary;
	double y = *(double*)&x;
	float z = y;
	*thread->output = *(uint32_t*)&z;
}
void instruction_156(thread_t* thread) {
	// 32-bit signed int to 32-bit float
	int32_t x = *thread->primary & 0xFFFFFFFF;
	float y = x;
	*thread->output = *(int32_t*)&y;
}
void instruction_157(thread_t* thread) {
	// 64-bit signed int to 64-bit float
	int64_t x = *thread->primary;
	double y = x;
	*thread->output = *(int64_t*)&y;
}
void instruction_158(thread_t* thread) {
	// 32-bit float to 32-bit signed int
	uint32_t x = *thread->primary & 0xFFFFFFFF;
	float y = *(float*)&x;
	int32_t z = y;
	*thread->output = *(int32_t*)&z;
}
void instruction_159(thread_t* thread) {
	// 64-bit float to 64-bit signed int
	uint64_t x = *thread->primary;
	double y = *(double*)&x;
	int64_t z = y;
	*thread->output = *(int64_t*)&z;
}
void instruction_160(thread_t* thread) { thread->primary = &thread->regs[0]; }
void instruction_161(thread_t* thread) { thread->primary = &thread->regs[1]; }
void instruction_162(thread_t* thread) { thread->primary = &thread->regs[2]; }
void instruction_163(thread_t* thread) { thread->primary = &thread->regs[3]; }
void instruction_164(thread_t* thread) { thread->primary = &thread->regs[4]; }
void instruction_165(thread_t* thread) { thread->primary = &thread->regs[5]; }
void instruction_166(thread_t* thread) { thread->primary = &thread->regs[6]; }
void instruction_167(thread_t* thread) { thread->primary = &thread->regs[7]; }
void instruction_168(thread_t* thread) { thread->primary = &thread->regs[8]; }
void instruction_169(thread_t* thread) { thread->primary = &thread->regs[9]; }
void instruction_170(thread_t* thread) { thread->primary = &thread->regs[10]; }
void instruction_171(thread_t* thread) { thread->primary = &thread->regs[11]; }
void instruction_172(thread_t* thread) { thread->primary = &thread->regs[12]; }
void instruction_173(thread_t* thread) { thread->primary = &thread->regs[13]; }
void instruction_174(thread_t* thread) { thread->primary = &thread->regs[14]; }
void instruction_175(thread_t* thread) { thread->primary = &thread->regs[15]; }
void instruction_176(thread_t* thread) { thread->secondary = &thread->regs[0]; }
void instruction_177(thread_t* thread) { thread->secondary = &thread->regs[1]; }
void instruction_178(thread_t* thread) { thread->secondary = &thread->regs[2]; }
void instruction_179(thread_t* thread) { thread->secondary = &thread->regs[3]; }
void instruction_180(thread_t* thread) { thread->secondary = &thread->regs[4]; }
void instruction_181(thread_t* thread) { thread->secondary = &thread->regs[5]; }
void instruction_182(thread_t* thread) { thread->secondary = &thread->regs[6]; }
void instruction_183(thread_t* thread) { thread->secondary = &thread->regs[7]; }
void instruction_184(thread_t* thread) { thread->secondary = &thread->regs[8]; }
void instruction_185(thread_t* thread) { thread->secondary = &thread->regs[9]; }
void instruction_186(thread_t* thread) { thread->secondary = &thread->regs[10]; }
void instruction_187(thread_t* thread) { thread->secondary = &thread->regs[11]; }
void instruction_188(thread_t* thread) { thread->secondary = &thread->regs[12]; }
void instruction_189(thread_t* thread) { thread->secondary = &thread->regs[13]; }
void instruction_190(thread_t* thread) { thread->secondary = &thread->regs[14]; }
void instruction_191(thread_t* thread) { thread->secondary = &thread->regs[15]; }
void instruction_192(thread_t* thread) { uint64_t a=thread->regs[15];if(a+1>thread->instruction_max)return;*thread->primary=loadval(memory+a+1, 1); }
void instruction_193(thread_t* thread) { uint64_t a=thread->regs[15];if(a+2>thread->instruction_max)return;*thread->primary=loadval(memory+a+1, 2); }
void instruction_194(thread_t* thread) { uint64_t a=thread->regs[15];if(a+3>thread->instruction_max)return;*thread->primary=loadval(memory+a+1, 3); }
void instruction_195(thread_t* thread) { uint64_t a=thread->regs[15];if(a+4>thread->instruction_max)return;*thread->primary=loadval(memory+a+1, 4); }
void instruction_196(thread_t* thread) { uint64_t a=thread->regs[15];if(a+5>thread->instruction_max)return;*thread->primary=loadval(memory+a+1, 5); }
void instruction_197(thread_t* thread) { uint64_t a=thread->regs[15];if(a+6>thread->instruction_max)return;*thread->primary=loadval(memory+a+1, 6); }
void instruction_198(thread_t* thread) { uint64_t a=thread->regs[15];if(a+7>thread->instruction_max)return;*thread->primary=loadval(memory+a+1, 7); }
void instruction_199(thread_t* thread) { uint64_t a=thread->regs[15];if(a+8>thread->instruction_max)return;*thread->primary=loadval(memory+a+1, 8); }
void instruction_200(thread_t* thread) { uint64_t a=thread->regs[15];if(a+1>thread->instruction_max)return;*thread->secondary=loadval(memory+a+1, 1); }
void instruction_201(thread_t* thread) { uint64_t a=thread->regs[15];if(a+2>thread->instruction_max)return;*thread->secondary=loadval(memory+a+1, 2); }
void instruction_202(thread_t* thread) { uint64_t a=thread->regs[15];if(a+3>thread->instruction_max)return;*thread->secondary=loadval(memory+a+1, 3); }
void instruction_203(thread_t* thread) { uint64_t a=thread->regs[15];if(a+4>thread->instruction_max)return;*thread->secondary=loadval(memory+a+1, 4); }
void instruction_204(thread_t* thread) { uint64_t a=thread->regs[15];if(a+5>thread->instruction_max)return;*thread->secondary=loadval(memory+a+1, 5); }
void instruction_205(thread_t* thread) { uint64_t a=thread->regs[15];if(a+6>thread->instruction_max)return;*thread->secondary=loadval(memory+a+1, 6); }
void instruction_206(thread_t* thread) { uint64_t a=thread->regs[15];if(a+7>thread->instruction_max)return;*thread->secondary=loadval(memory+a+1, 7); }
void instruction_207(thread_t* thread) { uint64_t a=thread->regs[15];if(a+8>thread->instruction_max)return;*thread->secondary=loadval(memory+a+1, 8); }
void instruction_208(thread_t* thread) { *thread->primary = byteswap(*thread->primary, 0); }
void instruction_209(thread_t* thread) { *thread->primary = byteswap(*thread->primary, 1); }
void instruction_210(thread_t* thread) { *thread->primary = byteswap(*thread->primary, 2); }
void instruction_211(thread_t* thread) { *thread->primary = byteswap(*thread->primary, 3); }
void instruction_212(thread_t* thread) { *thread->primary = byteswap(*thread->primary, 4); }
void instruction_213(thread_t* thread) { *thread->primary = byteswap(*thread->primary, 5); }
void instruction_214(thread_t* thread) { *thread->primary = byteswap(*thread->primary, 6); }
void instruction_215(thread_t* thread) { *thread->primary = byteswap(*thread->primary, 7); }
void instruction_216(thread_t* thread) { *thread->secondary = byteswap(*thread->secondary, 0); }
void instruction_217(thread_t* thread) { *thread->secondary = byteswap(*thread->secondary, 1); }
void instruction_218(thread_t* thread) { *thread->secondary = byteswap(*thread->secondary, 2); }
void instruction_219(thread_t* thread) { *thread->secondary = byteswap(*thread->secondary, 3); }
void instruction_220(thread_t* thread) { *thread->secondary = byteswap(*thread->secondary, 4); }
void instruction_221(thread_t* thread) { *thread->secondary = byteswap(*thread->secondary, 5); }
void instruction_222(thread_t* thread) { *thread->secondary = byteswap(*thread->secondary, 6); }
void instruction_223(thread_t* thread) { *thread->secondary = byteswap(*thread->secondary, 7); }
void instruction_224(thread_t* thread) {
	uint8_t n_bytes = 1;    // number of bytes to load
	if(*thread->secondary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->secondary, n_bytes)) { *thread->primary = read_main_mem_val(thread, *thread->secondary, n_bytes); return; }
	else if(!check_sys_region(thread->privacy_key, *thread->secondary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*thread->primary = memory[*thread->secondary];
}
void instruction_225(thread_t* thread) {
	uint8_t n_bytes = 2;    // number of bytes to load
	if(*thread->secondary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->secondary, n_bytes)) { *thread->primary = read_main_mem_val(thread, *thread->secondary, n_bytes); return; }
	else if(!check_sys_region(thread->privacy_key, *thread->secondary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*thread->primary = *(uint16_t*)&memory[*thread->secondary];
}
void instruction_226(thread_t* thread) {
	uint8_t n_bytes = 4;    // number of bytes to load
	if(*thread->secondary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->secondary, n_bytes)) { *thread->primary = read_main_mem_val(thread, *thread->secondary, n_bytes); return; }
	else if(!check_sys_region(thread->privacy_key, *thread->secondary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*thread->primary = *(uint32_t*)&memory[*thread->secondary];
}
void instruction_227(thread_t* thread) {
	uint8_t n_bytes = 8;    // number of bytes to load
	if(*thread->secondary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->secondary, n_bytes)) { *thread->primary = read_main_mem_val(thread, *thread->secondary, n_bytes); return; }
	else if(!check_sys_region(thread->privacy_key, *thread->secondary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*thread->primary = *(uint64_t*)&memory[*thread->secondary];
}
void instruction_228(thread_t* thread) {
	uint8_t n_bytes = 1;    // number of bytes to load
	if(*thread->primary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->primary, n_bytes)) { *thread->secondary = read_main_mem_val(thread, *thread->primary, n_bytes); return; }
	else if(!check_sys_region(thread->privacy_key, *thread->primary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*thread->secondary = memory[*thread->primary];
}
void instruction_229(thread_t* thread) {
	uint8_t n_bytes = 2;    // number of bytes to load
	if(*thread->primary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->primary, n_bytes)) { *thread->secondary = read_main_mem_val(thread, *thread->primary, n_bytes); return; }
	else if(!check_sys_region(thread->privacy_key, *thread->primary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*thread->secondary = *(uint16_t*)&memory[*thread->primary];
}
void instruction_230(thread_t* thread) {
	uint8_t n_bytes = 4;    // number of bytes to load
	if(*thread->primary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->primary, n_bytes)) { *thread->secondary = read_main_mem_val(thread, *thread->primary, n_bytes); return; }
	else if(!check_sys_region(thread->privacy_key, *thread->primary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*thread->secondary = *(uint32_t*)&memory[*thread->primary];
}
void instruction_231(thread_t* thread) {
	uint8_t n_bytes = 8;    // number of bytes to load
	if(*thread->primary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->primary, n_bytes)) { *thread->secondary = read_main_mem_val(thread, *thread->primary, n_bytes); return; }
	else if(!check_sys_region(thread->privacy_key, *thread->primary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*thread->secondary = *(uint64_t*)&memory[*thread->primary];
}
void instruction_232(thread_t* thread) {
	uint8_t n_bytes = 1;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] + n_bytes - 1 < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) *thread->primary = read_main_mem_val(thread, thread->regs[12], n_bytes);
	else { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[12] += n_bytes;
}
void instruction_233(thread_t* thread) {
	uint8_t n_bytes = 2;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] + n_bytes - 1 < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) *thread->primary = read_main_mem_val(thread, thread->regs[12], n_bytes);
	else { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[12] += n_bytes;
}
void instruction_234(thread_t* thread) {
	uint8_t n_bytes = 4;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] + n_bytes - 1 < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) *thread->primary = read_main_mem_val(thread, thread->regs[12], n_bytes);
	else { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[12] += n_bytes;
}
void instruction_235(thread_t* thread) {
	uint8_t n_bytes = 8;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] + n_bytes - 1 < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) *thread->primary = read_main_mem_val(thread, thread->regs[12], n_bytes);
	else { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[12] += n_bytes;
}
void instruction_236(thread_t* thread) {
	uint8_t n_bytes = 1;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] + n_bytes - 1 < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) *thread->secondary = read_main_mem_val(thread, thread->regs[12], n_bytes);
	else { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[12] += n_bytes;
}
void instruction_237(thread_t* thread) {
	uint8_t n_bytes = 2;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] + n_bytes - 1 < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) *thread->secondary = read_main_mem_val(thread, thread->regs[12], n_bytes);
	else { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[12] += n_bytes;
}
void instruction_238(thread_t* thread) {
	uint8_t n_bytes = 4;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] + n_bytes - 1 < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) *thread->secondary = read_main_mem_val(thread, thread->regs[12], n_bytes);
	else { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[12] += n_bytes;
}
void instruction_239(thread_t* thread) {
	uint8_t n_bytes = 8;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] + n_bytes - 1 < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) *thread->secondary = read_main_mem_val(thread, thread->regs[12], n_bytes);
	else { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[12] += n_bytes;
}
void instruction_240(thread_t* thread) {
	uint8_t n_bytes = 1;    // number of bytes to store
	if(*thread->primary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->primary, n_bytes)) { write_main_mem_val(thread, *thread->primary, *thread->secondary, n_bytes); return; }
	if(!check_mapped_region(thread->privacy_key, *thread->primary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	memory[*thread->primary] = *thread->secondary;
}
void instruction_241(thread_t* thread) {
	uint8_t n_bytes = 2;    // number of bytes to store
	if(*thread->primary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->primary, n_bytes)) { write_main_mem_val(thread, *thread->primary, *thread->secondary, n_bytes); return; }
	if(!check_mapped_region(thread->privacy_key, *thread->primary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*(uint16_t*)(&memory[*thread->primary]) = *thread->secondary;
}
void instruction_242(thread_t* thread) {
	uint8_t n_bytes = 4;    // number of bytes to store
	if(*thread->primary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->primary, n_bytes)) { write_main_mem_val(thread, *thread->primary, *thread->secondary, n_bytes); return; }
	if(!check_mapped_region(thread->privacy_key, *thread->primary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*(uint32_t*)(&memory[*thread->primary]) = *thread->secondary;
}
void instruction_243(thread_t* thread) {
	uint8_t n_bytes = 8;    // number of bytes to store
	if(*thread->primary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->primary, n_bytes)) { write_main_mem_val(thread, *thread->primary, *thread->secondary, n_bytes); return; }
	if(!check_mapped_region(thread->privacy_key, *thread->primary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*(uint64_t*)(&memory[*thread->primary]) = *thread->secondary;
}
void instruction_244(thread_t* thread) {
	uint8_t n_bytes = 1;	// number of bytes to store
	if(*thread->secondary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->secondary, n_bytes)) { write_main_mem_val(thread, *thread->secondary, *thread->primary, n_bytes); return; }
	if(!check_mapped_region(thread->privacy_key, *thread->secondary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	memory[*thread->secondary] = *thread->primary;
}
void instruction_245(thread_t* thread) {
	uint8_t n_bytes = 2;	// number of bytes to store
	if(*thread->secondary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->secondary, n_bytes)) { write_main_mem_val(thread, *thread->secondary, *thread->primary, n_bytes); return; }
	if(!check_mapped_region(thread->privacy_key, *thread->secondary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*(uint16_t*)(&memory[*thread->secondary]) = *thread->primary;
}
void instruction_246(thread_t* thread) {
	uint8_t n_bytes = 4;	// number of bytes to store
	if(*thread->secondary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->secondary, n_bytes)) { write_main_mem_val(thread, *thread->secondary, *thread->primary, n_bytes); return; }
	if(!check_mapped_region(thread->privacy_key, *thread->secondary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*(uint32_t*)(&memory[*thread->secondary]) = *thread->primary;
}
void instruction_247(thread_t* thread) {
	uint8_t n_bytes = 8;	// number of bytes to store
	if(*thread->secondary < SIZE_MAIN_MEM && !check_segfault(thread, *thread->secondary, n_bytes)) { write_main_mem_val(thread, *thread->secondary, *thread->primary, n_bytes); return; }
	if(!check_mapped_region(thread->privacy_key, *thread->secondary, n_bytes)) { thread->regs[13] |= SR_BIT_SEGFAULT; return; }
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	*(uint64_t*)(&memory[*thread->secondary]) = *thread->primary;
}
void instruction_248(thread_t* thread) {
	uint8_t n_bytes = 1;
	thread->regs[12] -= n_bytes;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) write_main_mem_val(thread, thread->regs[12], *thread->primary, n_bytes);
	else thread->regs[13] |= SR_BIT_SEGFAULT;
}
void instruction_249(thread_t* thread) {
	uint8_t n_bytes = 2;
	thread->regs[12] -= n_bytes;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) write_main_mem_val(thread, thread->regs[12], *thread->primary, n_bytes);
	else thread->regs[13] |= SR_BIT_SEGFAULT;
}
void instruction_250(thread_t* thread) {
	uint8_t n_bytes = 4;
	thread->regs[12] -= n_bytes;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) write_main_mem_val(thread, thread->regs[12], *thread->primary, n_bytes);
	else thread->regs[13] |= SR_BIT_SEGFAULT;
}
void instruction_251(thread_t* thread) {
	uint8_t n_bytes = 8;
	thread->regs[12] -= n_bytes;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) write_main_mem_val(thread, thread->regs[12], *thread->primary, n_bytes);
	else thread->regs[13] |= SR_BIT_SEGFAULT;
}
void instruction_252(thread_t* thread) {
	uint8_t n_bytes = 1;
	thread->regs[12] -= n_bytes;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) write_main_mem_val(thread, thread->regs[12], *thread->secondary, n_bytes);
	else thread->regs[13] |= SR_BIT_SEGFAULT;
}
void instruction_253(thread_t* thread) {
	uint8_t n_bytes = 2;
	thread->regs[12] -= n_bytes;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) write_main_mem_val(thread, thread->regs[12], *thread->secondary, n_bytes);
	else thread->regs[13] |= SR_BIT_SEGFAULT;
}
void instruction_254(thread_t* thread) {
	uint8_t n_bytes = 4;
	thread->regs[12] -= n_bytes;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) write_main_mem_val(thread, thread->regs[12], *thread->secondary, n_bytes);
	else thread->regs[13] |= SR_BIT_SEGFAULT;
}
void instruction_255(thread_t* thread) {
	uint8_t n_bytes = 8;
	thread->regs[12] -= n_bytes;
	thread->regs[13] &= (~SR_BIT_SEGFAULT);
	if(thread->regs[12] < SIZE_MAIN_MEM && !check_segfault(thread, thread->regs[12], n_bytes)) write_main_mem_val(thread, thread->regs[12], *thread->secondary, n_bytes);
	else thread->regs[13] |= SR_BIT_SEGFAULT;
}




void (*instruction_funcs[256])(thread_t*);

// fill the instruction_funcs array with addresses to instruction functions
void init_funcs() {
#define I(x) instruction_funcs[x] = &instruction_##x
	I(0); I(1); I(2); I(3); I(4); I(5); I(6); I(7); I(8); I(9);
	I(10); I(11); I(12); I(13); I(14); I(15); I(16); I(17); I(18); I(19);
	I(20); I(21); I(22); I(23); I(24); I(25); I(26); I(27); I(28); I(29);
	I(30); I(31); I(32); I(33); I(34); I(35); I(36); I(37); I(38); I(39);
	I(40); I(41); I(42); I(43); I(44); I(45); I(46); I(47); I(48); I(49);
	I(50); I(51); I(52); I(53); I(54); I(55); I(56); I(57); I(58); I(59);
	I(60); I(61); I(62); I(63); I(64); I(65); I(66); I(67); I(68); I(69);
	I(70); I(71); I(72); I(73); I(74); I(75); I(76); I(77); I(78); I(79);
	I(80); I(81); I(82); I(83); I(84); I(85); I(86); I(87); I(88); I(89);
	I(90); I(91); I(92); I(93); I(94); I(95); I(96); I(97); I(98); I(99);
	I(100); I(101); I(102); I(103); I(104); I(105); I(106); I(107); I(108); I(109);
	I(110); I(111); I(112); I(113); I(114); I(115); I(116); I(117); I(118); I(119);
	I(120); I(121); I(122); I(123); I(124); I(125); I(126); I(127); I(128); I(129);
	I(130); I(131); I(132); I(133); I(134); I(135); I(136); I(137); I(138); I(139);
	I(140); I(141); I(142); I(143); I(144); I(145); I(146); I(147); I(148); I(149);
	I(150); I(151); I(152); I(153); I(154); I(155); I(156); I(157); I(158); I(159);
	I(160); I(161); I(162); I(163); I(164); I(165); I(166); I(167); I(168); I(169);
	I(170); I(171); I(172); I(173); I(174); I(175); I(176); I(177); I(178); I(179);
	I(180); I(181); I(182); I(183); I(184); I(185); I(186); I(187); I(188); I(189);
	I(190); I(191); I(192); I(193); I(194); I(195); I(196); I(197); I(198); I(199);
	I(200); I(201); I(202); I(203); I(204); I(205); I(206); I(207); I(208); I(209);
	I(210); I(211); I(212); I(213); I(214); I(215); I(216); I(217); I(218); I(219);
	I(220); I(221); I(222); I(223); I(224); I(225); I(226); I(227); I(228); I(229);
	I(230); I(231); I(232); I(233); I(234); I(235); I(236); I(237); I(238); I(239);
	I(240); I(241); I(242); I(243); I(244); I(245); I(246); I(247); I(248); I(249);
	I(250); I(251); I(252); I(253); I(254); I(255);
#undef I
}

void exec_cycle(thread_t* thread) {
	if(thread->joining && !threads[thread->joining-1].killed) return;
	thread->joining = 0;	// no longer waiting for any threads to be killed
	thread->end_cyc = 0;	// end_cyc is not set at beginning of cycle
	// set the threads created by this thread in last cycle to come alive
	for(uint32_t i = 0; i < thread->n_created_threads; i++)
		threads[thread->created_threads[i]].killed = 0;

	if(thread->created_threads) free(thread->created_threads);
	thread->created_threads = 0;
	thread->n_created_threads = 0;
	uint64_t* pc = &thread->regs[15];
	uint64_t thread_id = thread->id;
	uint64_t prev_r11 = 0;
	// execute instructions until cycle end
	while(1) {
		uint64_t prev_pc = *pc;
		if(*pc < thread->instruction_min || *pc > thread->instruction_max) {
			kill_thread(thread);	// atttempting execution outside of instruction range; kill the thread
#if SHOW_INS_OUT_OF_RANGE
			printf("instruction memory range violation for thread %d (%d), exiting.\n", thread_id, *pc);
#endif
			return;
		}
		uint8_t instruction = memory[*pc];
		(*instruction_funcs[ instruction ])(thread);
		if(*pc != prev_pc) break;	// end cycle if the instruction modified the program counter
		(*pc)++;
#if STD_OUTPUT
		if(threads[thread_id].regs[11] != prev_r11) {
			putchar(threads[thread_id].regs[11]);
			fflush(stdout);
		}
		prev_r11 = threads[thread_id].regs[11];
#endif
		if(threads[thread_id].end_cyc) break;	// as soon as the first instruction that set end_cyc is executed, end cycle
		if(instruction >= 192 && instruction <= 207) {	// if a move instruction didn't result in manual change of the program counter (cycle would've ended), offset the PC past moved bytes
			if(instruction < 200) *pc += instruction - 191;	
			else *pc += instruction - 199;
		}
	}
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
	switch(button) {
		case GLFW_MOUSE_BUTTON_LEFT:
			if(action == GLFW_PRESS) mouse_buttons |= 0x4;
			else mouse_buttons &= (~0x4); break;
		case GLFW_MOUSE_BUTTON_RIGHT:
			if(action == GLFW_PRESS) mouse_buttons |= 0x2;
			else mouse_buttons &= (~0x2); break;
		case GLFW_MOUSE_BUTTON_MIDDLE:
			if(action == GLFW_PRESS) mouse_buttons |= 0x1;
			else mouse_buttons &= (~0x1); break;
	}
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
	scroll_x += xoffset;
	scroll_y += yoffset;
}

void cursor_pos_callback(GLFWwindow* window, double x_pos, double y_pos) {
	cursor_x += x_pos;
	cursor_y += y_pos;
	glfwSetCursorPos(window, 0, 0);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	uint32_t key_pos = 0; // key position in kbd_states (first bit is 0, at col 0 row 0)
	if(key >= GLFW_KEY_A && key <= GLFW_KEY_Z) { // letter key enumerators are in alphabetical order
		char* qwerty = "qwertyuiopasdfghjklzxcvbnm";
		uint8_t c = key - GLFW_KEY_A; // position in alphabet
		uint32_t i = 0; // index of letter in the (qwerty layout) kbd info
		for(; i < 26; i++) if(qwerty[i] == 'a'+c) break;
		key_pos = i + 38;
	} else if(key >= GLFW_KEY_0 && key <= GLFW_KEY_9) { // numeric key enumerators are in order; 0-9
		if(key == GLFW_KEY_0) key_pos = 34;
		else key_pos = (key - GLFW_KEY_1) + 25;
	} else {
		switch(key) {
			case GLFW_KEY_LEFT_SHIFT: case GLFW_KEY_RIGHT_SHIFT:
				key_pos = 5; break;
			case GLFW_KEY_TAB: key_pos = 6; break;
			case GLFW_KEY_ENTER: key_pos = 7; break;
			case GLFW_KEY_SPACE: key_pos = 8; break;
			case GLFW_KEY_CAPS_LOCK: key_pos = 9; break;
			case GLFW_KEY_ESCAPE: key_pos = 10; break;
			case GLFW_KEY_LEFT_CONTROL: case GLFW_KEY_RIGHT_CONTROL:
				key_pos = 11; break;
			case GLFW_KEY_BACKSPACE: key_pos = 12; break;
			case GLFW_KEY_LEFT_ALT: case GLFW_KEY_RIGHT_ALT:
				key_pos = 13; break;
			case GLFW_KEY_UP: key_pos = 14; break;
			case GLFW_KEY_DOWN: key_pos = 15; break;
			case GLFW_KEY_LEFT: key_pos = 16; break;
			case GLFW_KEY_RIGHT: key_pos = 17; break;
			case GLFW_KEY_LEFT_SUPER: case GLFW_KEY_RIGHT_SUPER:
				key_pos = 18; break;
			case GLFW_KEY_PAGE_UP: key_pos = 19; break;
			case GLFW_KEY_PAGE_DOWN: key_pos = 20; break;
			case GLFW_KEY_HOME: key_pos = 21; break;
			case GLFW_KEY_END: key_pos = 22; break;
			case GLFW_KEY_INSERT: key_pos = 23; break;
			case GLFW_KEY_DELETE: key_pos = 24; break;
			case GLFW_KEY_LEFT_BRACKET: key_pos = 64; break;
			case GLFW_KEY_RIGHT_BRACKET: key_pos = 65; break;
			case GLFW_KEY_SEMICOLON: key_pos = 66; break;
			case GLFW_KEY_APOSTROPHE: key_pos = 67; break;
			case GLFW_KEY_BACKSLASH: key_pos = 68; break;
			case GLFW_KEY_COMMA: key_pos = 69; break;
			case GLFW_KEY_PERIOD: key_pos = 70; break;
			case GLFW_KEY_SLASH: key_pos = 71; break;
			default: return;
		}
	}
	uint8_t col_bits[8];
	for(uint32_t i = 0; i < 8; i++) col_bits[i] = 0x80>>i;
	uint8_t* row = &kbd_states[key_pos / 8];
	uint8_t col = key_pos % 8;

	if(action == GLFW_PRESS) *row |= col_bits[col];
	else if(action == GLFW_RELEASE) *row &= (~col_bits[col]);
}

void window_size_callback(GLFWwindow* window, int width, int height) {
	window_width = width;
	window_height = height;
	glViewport(0,0,width,height);
}

// checks if the root_path variable is a valid root path string. returns 1 if valid, 0 otherwise.
uint8_t validate_root() {
	if(!validate_path(root_path)) {
		printf("Error: root_path string (%s) is not a valid path string\n", root_path);
		return 0;
	}
	if(root_path[0] != '/') {
		printf("Error: root_path string (%s) must start with /\n", root_path);
		return 0;
	}
	if(root_path[strlen(root_path)-1] == '/') {
		root_path = strdup(root_path);
		root_path[strlen(root_path)-1] = '\0';	// ensure root_path doesn't end with /
	}
	struct stat stat_buf;
	if(stat(root_path, &stat_buf) != 0 || !S_ISDIR(stat_buf.st_mode)) {
		printf("Error: root_path string (%s) is not a path to an existing directory\n", root_path);
		return 0;
	}
	return 1;
}

char* program_name;
uint8_t process_args(int argc, char* argv[]) {
	uint8_t invalid = 0, show_help = 0;
	int8_t cur_option = -1;
	for(uint32_t i = 1; i < argc; i++) {
		char* arg = argv[i];
		if(strcmp(arg, "-r") == 0)				cur_option = 0;
		else if(strcmp(arg, "-i") == 0)			show_program_info = 1;
		else if(strcmp(arg, "-h") == 0)			show_help = 1;
		else if(strcmp(arg, "--help") == 0) 	show_help = 1;
		else if(strcmp(arg, "--vsync") == 0)	enable_vsync = 1;
		else if(strcmp(arg, "-v") == 0)			show_about = 1;
		else if(cur_option == -1) {
			if(program_name) {	// program file can only be specified once
				invalid = 1;
				break;
			}
			program_name = arg;
		} else if(cur_option == 0) {
			root_path = arg;
			cur_option = -1;
		}
	}
	if(!program_name || argc == 1)
		invalid = 1;

	if(invalid || show_help) {
		if(invalid && !program_name) printf("Invalid usage. Please specify a program file to load.\n");
		else if(invalid) printf("Invalid usage.\n");
		printf("Usage: vm [options] file\n"
			"Options:\n"
			"   -r <dir>    Set <dir> as the root path directory\n"
			"   -i          Show info about the loaded program\n"
			"   -h, --help  Show this menu\n"
			"   --vsync     Enable VSync\n"
			"   -v          Show info about the VM\n"
		);
		if(invalid) return 0;
	}

	if(!validate_root()) return 0;
	return 1;
}

int main(int argc, char* argv[]) {
	if(!process_args(argc, argv)) return 1;

	if(show_about) {
		printf("Piculet VM (build %d)\n", BUILD_VER);
		printf("Developed by Gabriel Campbell\n");
		printf("- github.com/gabecampb\n\n");
	}

	uint32_t x = 1;
	char* c = (char*)&x;
	if(*c != 1) {
		printf("Error: System must be little-endian to use this virtual machine.\n");
		return 1;
	}

	init_funcs();
	init_threads(); // create thread 0

	if(!glfwInit()) {
		printf("glfwInit() failed. :(\n");
		return 1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	GLFWwindow* window = glfwCreateWindow(window_width, window_height, WINDOW_TITLE, NULL, NULL);
	if(!window) {
		printf("glfwCreateWindow() failed to create window. :(\n");
		return 1;
	}
	glfwMakeContextCurrent(window);

	glfwSetCursorPos(window, 0, 0);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetCursorPosCallback(window,cursor_pos_callback);
	glfwSetKeyCallback(window, key_callback);
	glfwSetWindowSizeCallback(window, window_size_callback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);

	// load initial program
	FILE* pfile = fopen(program_name,"r");
	if(!pfile) { printf("error opening initial program file \"%s\"; check that it exists and spelling is correct\n", program_name); return 1; }
	uint64_t program_size = 0;
	fseek(pfile, 0, SEEK_END);
	program_size = ftell(pfile);
	fseek(pfile, 0, 0);
	if(!program_size) { printf("initial program file \"%s\" has size of 0; exiting.\n", program_name); return 1; }
	uint8_t init_prog[program_size];
	fread(init_prog, program_size, 1, pfile);
	if(fclose(pfile)) { printf("error on closing initial program file \"%s\"\n", program_name); return 1; }
	if(show_program_info) {
		printf("loaded program \"%s\"\n", program_name);
		printf("size of program: %d bytes\n", (int)sizeof(init_prog));
	}
	memcpy(memory, init_prog, sizeof(init_prog));

	if(!enable_vsync)
		glfwSwapInterval(0);

	char* title_str = calloc(1,strlen(WINDOW_TITLE)+50); // + 50 to allow room for FPS counter
	double start_t = glfwGetTime();
	uint32_t frame_count = 0; // counts # of frames in a second

#if THR_0_RESTRICT_INS_RANGE
	threads[0].instruction_max = program_size-1;
#endif

	uint32_t tick = 0;
	clock_gettime(CLOCK_REALTIME, &start_tm);
	while(!threads[0].killed && !glfwWindowShouldClose(window)) {	// exit if thread 0 is killed or if the window is closed
		for(uint32_t i = 0; i < n_threads; i++) {
			if(threads[i].killed) continue;
			if(threads[i].sleep_duration_ns) {
				struct timespec tm;
				clock_gettime(CLOCK_REALTIME, &tm);
				if((tm.tv_sec-start_tm.tv_sec)*NS_PER_SEC + (tm.tv_nsec-start_tm.tv_nsec) <= threads[i].sleep_start_ns + threads[i].sleep_duration_ns)
					continue; // sleeping thread
				threads[i].sleep_start_ns = 0;
				threads[i].sleep_duration_ns = 0;
			}
			exec_cycle(&threads[i]);	// execute a cycle for this thread
		}

		if(gl_finish) { glFinish(); gl_finish = 0; }
		if(gl_swap) {
			glfwSwapBuffers(window);
#if SLEEP_AT_SWAP
			struct timespec ts;
			ts.tv_sec = 0;
			ts.tv_nsec = SLEEP_SWAP_MS*1000000;
			nanosleep(&ts,&ts);
#endif
#if SHOW_FPS
			double end_t = glfwGetTime();
			if(end_t - start_t >= .5) {
				sprintf(title_str, "%s - %d FPS", WINDOW_TITLE, frame_count*2);
				glfwSetWindowTitle(window, title_str);
				start_t = glfwGetTime();
				frame_count = 0;
			} else frame_count++;
#endif
			gl_swap = 0;
			glfwPollEvents();
		}
		if(tick > 500) {
			tick = 0;
			glfwPollEvents();
		}
		tick++;
	}
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
