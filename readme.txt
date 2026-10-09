========================================================================
    Aster Player
========================================================================

A desktop video player built with standard C++, C++/WinRT, WinUI 3, and the
Windows App SDK. Playback uses the Windows Media Player engine supplied by
the platform; supported formats and codecs therefore depend on Windows and
the codecs installed on the computer.

Features:
* Add multiple local video files to a playback queue.
* Select queued videos and move between previous and next videos.
* Play, pause, stop, seek, and jump forward or backward by 10 seconds.
* Adjust playback speed, volume, mute, repeat the current video, and use
  the video full-window mode.
* Track playback position and duration.

To build:
1. Open App1.slnx in Visual Studio with the C++ and Windows App SDK
   workloads installed.
2. Build the Debug or Release x64 configuration.
3. Run the application and use "Add videos" to choose one or more files.

This player keeps the native Windows playback stack and does not aim to
duplicate VLC's bundled codec coverage or every advanced VLC feature.

========================================================================
Learn more:
https://learn.microsoft.com/windows/apps/windows-app-sdk/
https://learn.microsoft.com/windows/apps/winui/winui3/
https://learn.microsoft.com/windows/uwp/audio-video-camera/media-playback
========================================================================
