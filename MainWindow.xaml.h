#pragma once

#include "MainWindow.g.h"
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Media.Playback.h>
#include <vector>

namespace winrt::App1::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        int32_t MyProperty();
        void MyProperty(int32_t value);

        void OpenVideo_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void PlayPause_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Stop_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Previous_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Next_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SeekBackward_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SeekForward_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void FullWindow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Mute_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void Playlist_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void SeekSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
        void VolumeSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
        void SpeedPicker_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);

    private:
        winrt::Windows::Media::Playback::MediaPlayer m_mediaPlayer{ nullptr };
        winrt::Microsoft::UI::Xaml::DispatcherTimer m_timer{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::hstring> m_playlistItems{ nullptr };
        std::vector<std::wstring> m_playlist;
        size_t m_currentIndex{ 0 };
        bool m_updatingSeekSlider{ false };
        bool m_updatingPlaylistSelection{ false };

        void PlayIndex(size_t index);
        void AdvancePlaylist(bool backwards);
        void UpdatePlaybackState();
        void UpdateTimeline();
        void SeekBy(double seconds);
        static winrt::hstring NormalizeFilePathForUri(std::wstring const& sourcePath);
        static winrt::hstring FormatTime(double seconds);
    };
}

namespace winrt::App1::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
