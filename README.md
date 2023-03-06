# SCI2FB
Converts Sierra SCI game patch resource files for the Yamaha FB-01/IMFC into 1 or 2 sysex bank files depending on how many are stored in the patch file. These banks can be recognized by the FB-01 directly (with software). This is useful for backing up and creating a library of Sierra FB-01 instrument banks. It's also handy for capturing individual FB-01 instrument voices from Sierra games to utilize in creating custom banks for SCI fangames (or even other Sierra games).

Usage:
sci2fb  patfile  [output_bank]

You can pass one or two parameters to the program. The input SCI patch file "patfile" is mandatory, but the output file(s) "output_bank" is optional. If given, "output_bank" will serve as the name for the sysex bank filename(s) and internal label(s) (up to 8 characters). If not, it will pull the name from "patfile" instead as a fallback.

File extensions are also optional.

"patfile" will first check for the file without an extension as passed. If not found it will check for the extension ".PAT". If THAT'S not found it will check for the extension ".002" (traditional extension for the actual resource "PATCH.002"). After that it will abort.

"output_bank" will always end up being a ".SYX" file regardless of the user-designated extension.

First release March 4, 2023
