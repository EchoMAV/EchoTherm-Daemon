development systems could be configurated different

these were the additional includes necessary to build project with vscode
these are configurated in the .vscode/c_cpp_properties.json file
but they can be set in the project parameterss

{
    "configurations": [
        {
            "name": "Linux",
            "includePath": [
                "${workspaceFolder}/**",
                "/usr/include",
                "/usr/include/c++/11",
                "/usr/include/x86_64-linux-gnu/c++/11",
                "/usr/include/opencv4"
            ],
            "defines": [],
            "compilerPath": "/usr/bin/gcc-11",
            "cStandard": "c17",
            "cppStandard": "c++17",
            "intelliSenseMode": "linux-gcc-x64"
        }
    ],
    "version": 4
}


Notes:
You should be able to build within vscode from a terminal windows
save
make clean
make all
sudo make install

the installer will check to make sure --daemon is not running which may prevent the copy
it will warn you of such
run echothermd --kill to stop daemon before install



