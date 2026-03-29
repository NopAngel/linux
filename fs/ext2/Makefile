# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux ext2-filesystem routines.
# Optimized for Fork/Modular builds.

obj-$(CONFIG_EXT2_FS) += ext2.o

ext2-y := balloc.o \
          dir.o \
          file.o \
          ialloc.o \
          inode.o \
          ioctl.o \
          namei.o \
          super.o \
          symlink.o \
          trace.o

ext2-$(CONFIG_EXT2_FS_XATTR)     += xattr.o xattr_user.o xattr_trusted.o

ext2-$(CONFIG_EXT2_FS_POSIX_ACL) += acl.o
ext2-$(CONFIG_EXT2_FS_SECURITY)  += xattr_security.o
CFLAGS_trace.o := -I$(src)
