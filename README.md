SourceCmd
=========

SourceCmd utility by Cryzbl

This tool allows you to run console commands in your favourite source engine game (without having to enter it in the console).
For now every time you execute a command the console will yell something about not being in the main thread, ignore that.
This works with clients in text-mode, but only grabs the first instance of the process, not all.

Full source code included.
Released under GPLv3 http://www.gnu.org/licenses/gpl.html

Usage
-------

### Interactive Mode (only supports 1 instance at a time):
* Launch your favourite source engine game. 
* Open SourceCmd.exe
* Type the name of the game's executable (for TF2 and CSS it's hl2.exe) and press enter.
* Type any console command you want.


### Command line usage (will run in every process it finds):
SourceCmd.exe \<process\> \<command\>

Example:

    SourceCmd.exe "hl2.exe" "echo Hello World!"
