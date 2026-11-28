# OpenOrbis PS4 toolchain environment (WSL). Source this file.
export OO_PS4_TOOLCHAIN="${OO_PS4_TOOLCHAIN:-$HOME/ps4dev/OpenOrbis/PS4Toolchain}"
export PATH="$HOME/bin:$PATH"   # clang/clang++/ld.lld -> LLVM 18
# PkgTool.Core (self-contained .NET Core 3) needs these on current Ubuntu
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
export LD_LIBRARY_PATH="$HOME/ps4dev/libssl11${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
OO_CFLAGS="--target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -isysroot $OO_PS4_TOOLCHAIN -isystem $OO_PS4_TOOLCHAIN/include"
export OO_CFLAGS
