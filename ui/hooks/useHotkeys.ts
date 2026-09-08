import { useEffect } from "react";
import { useAppStore } from "../store";

export function useGlobalHotkeys() {
  const activeTab = useAppStore((s) => s.activeTab);
  const audioToggle = useAppStore((s) => s.audioToggle);
  const audioSwitchChannel = useAppStore((s) => s.audioSwitchChannel);
  const audioStatus = useAppStore((s) => s.audioStatus);
  const duplicatePairs = useAppStore((s) => s.duplicatePairs);
  const activePairIndex = useAppStore((s) => s.activePairIndex);
  const setActivePairIndex = useAppStore((s) => s.setActivePairIndex);
  const resolveDuplicate = useAppStore((s) => s.resolveDuplicate);
  const albums = useAppStore((s) => s.albums);
  const selectedAlbumKey = useAppStore((s) => s.selectedAlbumKey);
  const setSelectedAlbumKey = useAppStore((s) => s.setSelectedAlbumKey);
  const approveAlbum = useAppStore((s) => s.approveAlbum);
  const approveTrack = useAppStore((s) => s.approveTrack);
  const skipItem = useAppStore((s) => s.skipItem);
  const selectCandidate = useAppStore((s) => s.selectCandidate);
  const isLogsOpen = useAppStore((s) => s.isLogsOpen);
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

      // Grave Accent / Tilde: Toggle Logs
      if (e.key === "`" || e.key === "~" || e.key === "ё" || e.key === "Ё") {
        e.preventDefault();
        toggleLogs();
        return;
      }

      // Escape: Close logs drawer if open
      if (e.key === "Escape") {
        if (isLogsOpen) {
          e.preventDefault();
          toggleLogs();
          return;
        }
        if (activeTab === "inspector" && selectedAlbumKey) {
          const curAlbum = albums.find((a) => a.albumKey === selectedAlbumKey);
          if (curAlbum) {
            e.preventDefault();
            skipItem(curAlbum.referenceIndex, true);
            return;
          }
        }
      }

      // Space: Toggle Audio
      if (e.code === "Space") {
        e.preventDefault();
        audioToggle();
        return;
      }

      // Duplicate comparison shortcuts
      if (activeTab === "duplicates" && duplicatePairs.length > 0) {
        // ArrowUp / ArrowLeft: Previous pair
        if (e.key === "ArrowUp" || e.key === "ArrowLeft") {
          e.preventDefault();
          setActivePairIndex(Math.max(0, activePairIndex - 1));
          return;
        }

        // ArrowDown / ArrowRight: Next pair
        if (e.key === "ArrowDown" || e.key === "ArrowRight") {
          e.preventDefault();
          setActivePairIndex(Math.min(duplicatePairs.length - 1, activePairIndex + 1));
          return;
        }

        // Tab: Switch duplicate deck A / B
        if (e.key === "Tab") {
          e.preventDefault();
          const next = audioStatus.activeChannel === "a" ? "b" : "a";
          audioSwitchChannel(next);
          return;
        }

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
      if (activeTab === "inspector" && albums.length > 0) {
        // Arrow navigation between albums
        if (e.key === "ArrowUp") {
          e.preventDefault();
          const curIdx = albums.findIndex((a) => a.albumKey === selectedAlbumKey);
          if (curIdx > 0) {
            setSelectedAlbumKey(albums[curIdx - 1].albumKey);
          }
          return;
        }
        if (e.key === "ArrowDown") {
          e.preventDefault();
          const curIdx = albums.findIndex((a) => a.albumKey === selectedAlbumKey);
          if (curIdx < albums.length - 1) {
            setSelectedAlbumKey(albums[curIdx + 1].albumKey);
          }
          return;
        }

        const curAlbum = albums.find((a) => a.albumKey === selectedAlbumKey);
        if (curAlbum) {
          // Tab: Cycle through metadata candidates
          if (e.key === "Tab" && curAlbum.candidates.length > 1) {
            e.preventDefault();
            const candIdx = curAlbum.selectedCoverChoice % curAlbum.candidates.length;
            selectCandidate(curAlbum.referenceIndex, (candIdx + 1) % curAlbum.candidates.length, true);
            return;
          }

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
    isLogsOpen,
    audioToggle,
    audioSwitchChannel,
    setActivePairIndex,
    resolveDuplicate,
    setSelectedAlbumKey,
    approveAlbum,
    approveTrack,
    skipItem,
    selectCandidate,
    toggleLogs,
  ]);
}
