# Piculet

This is the public release of an old project of mine, that I worked on from
Sept 2018 to Jan 2023.

Along with the specification, this repo contains the assembler and the VM. I
have also made a programming language along with graphics API libraries and
other things, but it is hidden for now as I am using it to develop an OS on my
new ISA. Piculet is more of bytecode than an ISA, and it's not a very good one
either. Nonetheless, it is my most impressive (nearly) complete project.

## About This Release

This was my main hobby project for a few years. I do not write my code like this
anymore and I'm aware that this project has various flaws.

I am releasing it simply because it's an interesting project. A lot of very
specific design choices were made when I was working on the graphics side of
the spec, so many that I cannot remember them all. Think: stuff like 7 instead
of 8 min supported texture units so that an extra would be available if an
early version GLES implementation having only 8 TUs wanted emulate MRT support
by repeating render passes multiple times. A LOT of work went into designing
the graphics API, which is quite closely modeled after modern graphics APIs.

In addition to the public maybe finding it fun to play with, releasing it here
in this complete, single repo format allows me to feel some kind of closure
with this project that I've long moved on from.

## Licensing

For the specification, all rights are reserved. You can implement a VM based on
it, but not for commercial use.

For the VM, assembler, and code examples, the MIT license applies. I don't
care what you do with it as long as attribution is given.

## Features

Not all features defined by the spec are implemented. Mostly just the minimal
feature set is supported. Regardless, that is quite a large amount of stuff.

Compute shaders are supposed to be implemented (by GPU or emulation on CPU),
but I never implemented them.

## Assembler Usage

To build the assembler, run:

`cc asm.c -o asm`

To then run an assembly file through the assembler, run:

`./asm file.s`

This will result in an `out.bin` file being created.


Additionally, to output all assembled shaders into shader bytecode files
(`_shader0`, `_shader1`, ...), you can run:

`./asm file.s -s`

To print unused label warnings, run:

`./asm file.s -v`

## VM Usage

Building the VM is easy. GLFW 3 and OpenGL 3.1+ are required. Tested with clang
and gcc compilers on Arch, Ubuntu, and FreeBSD.

`cc vm.c -o vm -lGL -lglfw -lm`

The VM can then be run as `./vm file`, where the file specified is the program
to run.

## Demos

The `demos` folder contains a few demos, which are really just old hand-written
assembly programs that I was using for testing the VM. `push.s`, `quad.s`, and
`texture.s` examples all use a push constant for supplying data to the shader.
`uniform.s` uses a UBO for supplying data to the shader.

The assembly files weren't cleaned up - copied exactly as they were in my old
folders. There are commented out bits of code (mostly in shaders) that do extra
stuff which I left in because it gives you more view of what the shader
assembler looks like.

## That's All?

Yes, unfortunately that's all the documentation I have for this at the moment.
This isn't really a project I work on anymore. Read the spec if you want more
info, that's the biggest thing here.
