# WriteWord
Simple MDI Notepad. Allows for multiple notepad windows in one main container, offering efficient manageability of multiple notepad windows.

Created in Dev-C++ 4.9.9.2.

<img width="1440" height="780" alt="WRITEWORD" src="https://github.com/user-attachments/assets/93ca77fc-222d-44ed-a176-c466c845b2ca" />

## Update for version 2: Editing large documents

WriteWord uses the Windows Rich Edit 2.0 control in **plain-text mode**.
It streams text files when opening and saving, so files no longer need a
second document-sized buffer. The old EDIT control's small default input
limit is removed; the remaining upper bound is the Rich Edit control's
signed 32-bit text range (approximately 2 GB of text) and available system
memory. Files larger than the supported range are rejected, not truncated.
