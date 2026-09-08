export interface Candidate {
  source: string;
  title: string;
  artist: string;
  album: string;
  year: string;
  confidenceScore: number;
}

export interface TrackItem {
  index: number;
  trackNo: string;
  title: string;
  artist: string;
  album: string;
  duration: number;
  hasLyrics: boolean;
  hasSyncedLyrics: boolean;
  lyricsOriginal: string;
  lyricsRomaji: string;
  lyricsEnglish: string;
  currentLyrics: string;
  filePath: string;
  isProcessed: boolean;
}

export interface AlbumGroup {
  albumKey: string;
  album: string;
  artist: string;
  year: string;
  trackCount: number;
  confidenceScore: number;
  matchTierName: string;
  hasConflict: boolean;
  selectedCoverChoice: number;
  hasLocalCover: boolean;
  hasOnlineCover: boolean;
  onlineCoverSource: string;
  referenceIndex: number;
  candidates: Candidate[];
  tracks: TrackItem[];
}

export interface DuplicatePair {
  id: string;
  trackA_path: string;
  relA: string;
  extA: string;
  durA: number;
  trackB_path: string;
  relB: string;
  extB: string;
  durB: number;
  similarity: number;
  offset: number;
}

export interface AudioStatus {
  isPlaying: boolean;
  activeChannel: "a" | "b";
  currentTime: number;
  duration: number;
  volume: number;
}

export interface TagScanProgress {
  isScanning: boolean;
  done: number;
  total: number;
  fraction: number;
  speed: number;
  eta: string;
  elapsed: string;
}

export interface MirrorProgress {
  isRunning: boolean;
  completedTasks: number;
  totalTasks: number;
  createdFolders: number;
  copiedFallbacks: number;
  message: string;
}

export interface DatabaseTrack {
  id: number;
  artist: string;
  album: string;
  title: string;
  trackNo: string;
  year: string;
  durationSec: number;
  format: string;
  bitrateKbps: number;
  status: number;
  relPath: string;
}

export interface DatabaseStats {
  totalTracks: number;
  downloadedTracks: number;
  missingTracks: number;
}

export interface SystemStats {
  duplicatesCount: number;
  unresolvedTagsCount: number;
  totalTracks: number;
  downloadedTracks: number;
  missingTracks: number;
  isTagScanning: boolean;
  isDuplicateScanning: boolean;
  isMirroring: boolean;
}

export interface Settings {
  toSortDir: string;
  outputDir: string;
  flacDir: string;
  mp3Dir: string;
  acoustIdKey: string;
  discogsToken: string;
}
