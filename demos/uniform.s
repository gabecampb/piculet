;	Copyright (c) 2021-2024 Gabriel Campbell
;	
;	Permission is hereby granted, free of charge, to any person obtaining
;	a copy of this software and associated documentation files (the
;	"Software"), to deal in the Software without restriction, including
;	without limitation the rights to use, copy, modify, merge, publish,
;	distribute, sublicense, and/or sell copies of the Software, and to
;	permit persons to whom the Software is furnished to do so, subject to
;	the following conditions:
;	
;	The above copyright notice and this permission notice shall be
;	included in all copies or substantial portions of the Software.
;	
;	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
;	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
;	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
;	NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
;	LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
;	OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
;	WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

; setup VBO (store ID in R3)
MOV R0 2 		; 2 is the ID for VBO type in object generation
GEN R0 R1 R3 	; generate object, store ID in R3
BIND R3 R3 		; bind the generated VBO
MOV R1 84 		; VBO has size of 84 bytes
BALLOC R3 R1 	; allocate, for VBO, 84 bytes
MOV R0 VBO_DATA ; R0 is the address of VBO data
MOV R2 0 		; R2 = 0 (the ID for VBO type in map instruction)
MAP R2 R2 		; VBO store is mapped to address stored in R2
MCOPY R0 R1 R2 	; copy data from VBO_DATA to mapped VBO
MOV R1 0
MAP R1 R0 		; unmap; 2nd reg is unused in this case

; setup VAO (store ID in R4)
MOV R0 1 		; 1 is the ID for VAO type in object generation
MOV R1 ATTRIB_LAYOUT_DATA
GEN R0 R1 R4 	; generate object, store ID in R4

; setup vertex shader (store ID in R5)
MOV R0 19 		; 19 is the ID for vtx shader type in object gen.
GEN R0 R0 R5 	; 2nd reg is unused in this case
BIND R5 R5 		; bind the generated vertex shader
MOV R0 VTX_SHADER 	; R0 is the address of vtx shader source
MOV R1 PXL_SHADER 	; R1 is the address of pxl shader source
SUB R1 R0 R1 	; R1 = R1 - R0 (number of bytes for vertex shader)
BALLOC R5 R1 	; allocate bytes for vtx shader
MOV R2 6 		; R2 = 6 (ID for shader type in map instruction)
MAP R2 R2 		; vtx shader store mapped to address stored in R2
MCOPY R0 R1 R2 	; copy data from VTX_SHADER -> mapping
MOV R1 6
MAP R1 R0 		; unmap; 2nd reg is unused in this case

; setup pixel shader (store ID in R6)
MOV R0 20 		; 20 is the ID for pxl shader type in object gen.
GEN R0 R0 R6 	; 2nd reg is unused in this case
BIND R6 R6 		; bind the generated pixel shader
MOV R0 PXL_SHADER 	; R0 is the address of pxl shader source
MOV R1 PROGRAM_END 	; R1 is the last address
SUB R1 R0 R1 	; R1 = R1 - R0 (number of bytes for pixel shader)
BALLOC R6 R1 	; allocate bytes for pxl shader
MOV R2 6 		; R2 = 6 (ID for shader type in map instruction)
MAP R2 R2 		; pxl shader store mapped to address stored in R2
MCOPY R0 R1 R2 	; copy data from PXL_SHADER -> mapping
MOV R1 6
MAP R1 R0 		; unmap; 2nd reg is unused in this case

; store vertex shader ID in pipeline data (PIPE_DATA)
MOV R0 PIPE_DATA
STRD R5 R0

; store pixel shader ID in pipeline data (PIPE_DATA)
MOV R0 PIPE_DATA
MOV R1 8
ADD R0 R1 R0
STRD R6 R0

; store VAO ID in pipeline data (PIPE_DATA)
MOV R0 PIPE_DATA
MOV R1 16
ADD R0 R1 R0
STRD R4 R0

; create descriptor set layout (store ID in R9)
MOV R9 18
MOV R7 LAYOUT_INFO
GEN R9 R7 R9
MOV R7 PIPE_DATA
MOV R10 29
ADD R7 R10 R7
STRD R9 R7 ; set descriptor set layout ID at PIPE_DATA+29

; create descriptor set (store ID in R9)
MOV R8 17
GEN R8 R9 R9
BIND R9 R9

; create uniform buffer (store ID in R7)
MOV R0 6
GEN R0 R0 R7
BIND R7 R7
MOV R0 20
BALLOC R7 R0		; allocate UBO data (20 bytes)
MOV R0 2			; ID for uniform buffer in map instruction
MOV R1 UBO_DATA
MAP R0 R8			; store address of mapping in R8
MOV R0 20
MCOPY R1 R0 R8	 	; copy data from UBO_DATA to mapped UBO
MOV R0 2
MAP R0 R0			; unmap the uniform buffer

; create uniform descriptor, bind UBO to it (store new uniform descriptor ID in R7)
MOV R0 14
GEN R0 R0 R0
BIND R0 R0
BDSC R7 R0 		; bind UBO (R7) to the bound uniform descriptor
REGCOPY R0 R7	; R7 is now the new uniform descriptor

; set uniform descriptor ID in UPDATE_INFO
MOV R0 UPDATE_INFO
MOV R8 4
ADD R0 R8 R8
STRD R7 R8

; bind uniform descriptor to the descriptor set
CLR R0
MOV R7 UPDATE_INFO
UDSC R0 R7

; create raster pipeline (store ID in R7)
MOV R0 26 		; 26 is the ID for raster PL type in object gen.
MOV R1 PIPE_DATA
GEN R0 R1 R7

; create command buffer (store ID in R8)
MOV R0 0 		; 0 is the ID for CBO type in object generation
GEN R0 R4 R8

; store CBO ID in queue submissions
MOV R0 Q_SUBMITS
MOV R1 6
ADD R0 R1 R0
STRD R8 R0

; bind the command buffer
BIND R8 R8

; record pipeline bind command
BPIPE R7

; record VBO binding command
BSVI R3 R0

; record set binding command
CLR R0
BSVI R9 R0

; record color buffer clear command
MOV R0 CLEAR_COLOR
MOV R1 1
CBUFF R1 R0

; record depth buffer clear command
MOV R0 CLEAR_DEPTH
MOV R1 9
CBUFF R1 R0

; record draw command
MOV R0 DRAW_CALL_DATA
DRAW R0



CLR R3 ; number of frames executed

; begin the main loop
MAIN_LOOP:
	; do logic/updates here
	MOV R0 2
	MAP R0 R1
	MOV R4 16
	ADD R1 R4 R1
	ITOF R3 R4
	STR R4 R1		; frame count
	MAP R0 R0 		; unmap
	INCR R3

	; submit commands
	MOV R0 Q_SUBMITS
	GSUBMIT R0
	FCMDS
	SWAP
	MOV R1 16000000		; amount of time to sleep each frame; 16 ms
	SLEEP R1
	MOV R15 MAIN_LOOP 	; jump to beginning of loop

VBO_DATA: 	; vertex format is V3C4
; first vertex
.0
-.5
.0
0.
0.
1.
1.
; second vertex
-.5
.5
0.
0.
1.
0.
1.
; third vertex
.5
.5
.0
1.
0.
0.
1.

UBO_DATA:
.2		; a vec4
.2
.2
.2
5.		; time variable

Q_SUBMITS: 	; must be 14 bytes
H 0 ; 2
W 0 ; 6
D 0 ; 14

LAYOUT_INFO:
W 0		; 1 descriptor binding
W 2		; binding point
B 0		; this is a uniform descriptor

UPDATE_INFO:
W 2		; binding point
D 0		; ID of descriptor

CLEAR_COLOR:
.05
.05
.05
1.

CLEAR_DEPTH:
1.

ATTRIB_LAYOUT_DATA:
H 1 		; # of attributes
D #1C 		; vtx stride
H 2 		; attrib ID
D 0 		; attrib offset
B 2 		; attrib fmt
H 3 		; attrib ID
D #C 		; attrib offset
B 3 		; attrib fmt

DRAW_CALL_DATA:
B 0 		; is indexed draw call?
W 3 		; # of indices
W 0 		; starting index
W 0 		; # of instances

PIPE_DATA:
D 0		; v shader
D 0		; p shader
D 0		; vao
B 0		; culled winding
B 0		; primitive type
B 0		; # push constant bytes
H 1		; # descriptor sets
D 0		; set layout ID
H 0		; depth pass, depth enabled
B 255	; stencil ref
W 0 	; 44; stencil pass, sfail, dfail, sfail_dfail
W 0 	; stencil: func_mask, write_mask, stencil_ref, stencil_pass,
W 0 	; sfail, dfail, sfail_dfail, func_mask
B 0		; stencil write_mask
B #F 	; color write mask
B 0		; # of enabled color attachments
B 0		; color blending operation (add)
B 0		; color blending src factor (one)
B 1		; color blending dst factor (zero)
B 0		; alpha blending operation (add)
B 0		; alpha blending src factor (one)
B 1		; alpha blending dst factor (zero)

SHADERSTART
VTX_SHADER:
SMOOTH OUT VEC4 COLOR
LOC 2 IN VEC3 V_POS
LOC 3 IN VEC4 V_COL
UNIFORM_BLOCK 0 2			; set 0, binding 2
	UNIFORM VEC4 A_VECTOR 1
	UNIFORM FLOAT TIME 1
CLOSE
MAIN
	VEC4 FINAL 1
	ASSIGN FINAL 0 0 V_POS NO_IDX 0
	ASSIGN FINAL 0 1 V_POS NO_IDX 1
	ASSIGN FINAL 0 2 V_POS NO_IDX 2
	ASSIGN_CONST FINAL 0 3 1.0

	FLOAT T 1
	FLOAT X 1
	ASSIGN T 0 NO_IDX TIME 0 NO_IDX
	DIV_CONST T 0 NO_IDX T 0 NO_IDX 30.
	ASSIGN X 0 NO_IDX T 0 NO_IDX
	SCALAROP_SIN T 0 NO_IDX
	SCALAROP_COS X 0 NO_IDX

	; scale
	MULT FINAL 0 0 FINAL 0 0 T 0 NO_IDX
;	MULT FINAL 0 1 FINAL 0 1 T 0 NO_IDX

	MULT_CONST T 0 NO_IDX T 0 NO_IDX .5
	MULT_CONST X 0 NO_IDX X 0 NO_IDX .5

	; translation
	ADD FINAL 0 0 FINAL 0 0 T 0 NO_IDX
	ADD FINAL 0 1 FINAL 0 1 X 0 NO_IDX

;	SCALAROP_TAN FINAL 0 0
;	VECOP_SIN FINAL 0

;	FLOAT W_BIAS 1
;	ASSIGN W_BIAS 0 NO_IDX V_COL NO_IDX 0
;	DIV_CONST W_BIAS 0 NO_IDX W_BIAS 0 NO_IDX 999999999. ; bottom left
;	ADD FINAL 0 3 FINAL 0 3 W_BIAS 0 NO_IDX ; final.w = final.w + w_bias
;	ASSIGN W_BIAS 0 NO_IDX V_COL NO_IDX 1
;	DIV_CONST W_BIAS 0 NO_IDX W_BIAS 0 NO_IDX 999999999. ; top
;	ADD FINAL 0 3 FINAL 0 3 W_BIAS 0 NO_IDX ; final.w = final.w + w_bias
;	ASSIGN W_BIAS 0 NO_IDX V_COL NO_IDX 2
;	DIV_CONST W_BIAS 0 NO_IDX W_BIAS 0 NO_IDX 999999999. ; bottom right
;	ADD FINAL 0 3 FINAL 0 3 W_BIAS 0 NO_IDX ; final.w = final.w + w_bias

	VTX_OUT FINAL 0
	ASSIGN COLOR NO_IDX 255 V_COL NO_IDX 255 ; assign whole vector (vec element must be > component count; attributes do not have array indices since they can't be arrays)
PXL_SHADER:
IN VEC4 COLOR
LOC 0 OUT VEC4 FINAL
UNIFORM_BLOCK 0 2           ; set 0, binding 2
    UNIFORM VEC4 A_V 1
    UNIFORM FLOAT A_T 1
CLOSE
MAIN
	ASSIGN FINAL NO_IDX 255 COLOR NO_IDX 255
;	SWIZZLE FINAL NO_IDX yxxw ; swizzle test
PROGRAM_END:
SHADEREND
