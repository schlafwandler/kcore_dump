# A simple linux memory acquisition tool

kcore\_dump is a simple tool to create a physical memory dump of a running x86-64 
Linux system from userland.
It does so by extracting the relevant memory ranges from /proc/kcore.
The dump is written to disk in the [LiME file format](https://github.com/504ensicsLabs/LiME/blob/master/doc/README.md#lime-memory-range-header-version-1-specification-).

See [this blog post](https://schlafwandler.github.io/posts/dumping-/proc/kcore/) for details.

# Usage

## Build

```
git clone https://github.com/schlafwandler/kcore_dump.git
cd kcore_dump/
gcc -o kcore_dump kcore_dump.c
```

## Use
```
sudo ./kcore_dump <outfile>
```

# Project status
This tool was written to demonstrate the process of extracting a memory dump 
from /proc/kcore.
It has undergone only minimal testing (as in: works on my machine) and should not 
be considered a ready-to-use forensics tool.

Let me repeat this:

THIS TOOL HAS NOT UNDERGONE MUCH TESTING!

USE IT AT YOUR OWN RISK!

