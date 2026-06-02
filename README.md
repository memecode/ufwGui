# ufwGui
[Lgi](https://github.com/memecode/Lgi) GUI for the Linux [ufw firewall](https://help.ubuntu.com/community/UFW).
In very early stages of roughing out the design.

# Building:

Something like this:

    git clone https://github.com/memecode/lgi.git lgi/trunk
    git clone https://github.com/memecode/ufwGui.git
    cd ufwGui
    mkdir build
    cd build
    cmake -G Ninja ..

That should get you the binaries `ufwGui` and `ufwWorker`. 

# Run:

From the build folder:

    ./ufwGui

This will prompt you for your admin password, as `ufwWorker` runs with root permissions. Once the worker
is up and running it will interface the gui (non-root) with `ufw`.

It should look something like this:

![ufwGui Screenshot](https://i.imgur.com/n7P3qgr.png)

# Features:

What is supported:

- View the existing rules.
- Deleting rules.

What I'm planning to support is:

- Added basic new rules. Not sure how far I'll go with this...