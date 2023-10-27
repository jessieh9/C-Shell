# C-Shell
Experimenting with system design by building a basic C version of a shell that supports Linux commands and personally defined commands

Supports local executables and seven built in commands
- cd <directory> (changes directory to specified path)
- bg <job_id | pid> (brings a process with specified job_id or pid from Stopped into background)
- fg <job_id | pid> (brings a process with specified job_id or pid from Stopped to Running or from bg to fg)
- pwd (shows current working directory)
- jobs (shows the current processes)
- kill <job_id | pid> (kills the specified process)
- quit (quit the program)

Signal handlers are implemented and this is still a test version.
