import { useEffect } from "react";
import { useAppStore } from "../store";

export function useGlobalHotkeys() {
  const activeTab = useAppStore((s) => s.activeTab);
  const audioToggle = useAppStore((s) => s.audioToggle);
  const audioSwitchChannel = useAppStore((s) => s.audioSwitchChannel);
  const audioStatus = useAppStore((s) => s.audioStatus);
  const duplicatePairs = useAppStore((s) => s.duplicatePairs);
  const activePairIndex = useAppStore((s) => s.activePairIndex);
  const resolveDuplicate = useAppStore((s) => s.resolveDuplicate);
  const albums = useAppStore((s) => s.albums);
  const selectedAlbumKey = useAppStore((s) => s.selectedAlbumKey);
  const approveAlbum = useAppStore((s) => s.approveAlbum);
  const approveTrack = useAppStore((s) => s.approveTrack);
  const toggleLogs = useAppStore((s) => s.toggleLogs);

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      // Don't trigger shortcuts if user is typing in an input or textarea
      const target = e.target as HTMLElement;
      if (
        target.tagName === "INPUT" ||
        target.tagName === "TEXTAREA" ||
        target.isContentEditable
      ) {
        if (e.key === "Escape") {
          target.blur();
        }
        return;
      }

      // Space: Toggle Audio
      if (e.code === "Space") {
        e.preventDefault();
        audioToggle();
        return;
      }

      // Tab: Switch duplicate deck A / B
      if (e.key === "Tab") {
        e.preventDefault();
        if (activeTab === "duplicates") {
          const next = audioStatus.activeChannel === "a" ? "b" : "a";
          audioSwitchChannel(next);
        }
        return;
      }

      // Duplicate comparison shortcuts
      if (activeTab === "duplicates" && duplicatePairs.length > 0) {
        if (e.key === "a" || e.key === "A" || e.key === "ф" || e.key === "Ф") {
          if (e.ctrlKey) {
            audioSwitchChannel("a");
          } else {
            resolveDuplicate(activePairIndex, "keep_a");
          }
          return;
        }
        if (e.key === "b" || e.key === "B" || e.key === "и" || e.key === "И") {
          if (e.ctrlKey) {
            audioSwitchChannel("b");
          } else {
            resolveDuplicate(activePairIndex, "keep_b");
          }
          return;
        }
        if (e.key === "s" || e.key === "S" || e.key === "ы" || e.key === "Ы") {
          resolveDuplicate(activePairIndex, "skip");
          return;
        }
      }

      // Tag Inspector shortcuts
      if (activeTab === "inspector" && selectedAlbumKey) {
        const curAlbum = albums.find((a) => a.albumKey === selectedAlbumKey);
        if (curAlbum) {
          if (e.key === "Enter" && e.ctrlKey) {
            e.preventDefault();
            approveAlbum(curAlbum.referenceIndex);
            return;
          }
          if (e.key === "Enter" && !e.ctrlKey) {
            e.preventDefault();
            const firstUnprocessed = curAlbum.tracks.find((t) => !t.isProcessed);
            if (firstUnprocessed) {
              approveTrack(firstUnprocessed.index);
            }
            return;
          }
        }
      }

      // Grave Accent / Tilde: Toggle Logs
      if (e.key === "`" || e.key === "~" || e.key === "ё" || e.key === "Ё") {
        toggleLogs();
        return;
      }
    };

    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [
    activeTab,
    audioStatus,
    duplicatePairs,
    activePairIndex,
    albums,
    selectedAlbumKey,
    audioToggle,
    audioSwitchChannel,
    resolveDuplicate,
    approveAlbum,
    approveTrack,
    toggleLogs,
  ]);
}
