import numpy as np
import soundfile as sf
import librosa

def compare_audio_files(file1, file2, duration_sec=30):
    try:
        # Load first 30 seconds of both files
        y1, sr1 = librosa.load(file1, sr=22050, duration=duration_sec)
        y2, sr2 = librosa.load(file2, sr=22050, duration=duration_sec)

        # Compute MFCCs (spectral envelope)
        mfcc1 = librosa.feature.mfcc(y=y1, sr=sr1, n_mfcc=20)
        mfcc2 = librosa.feature.mfcc(y=y2, sr=sr2, n_mfcc=20)

        # Truncate to same length
        min_len = min(mfcc1.shape[1], mfcc2.shape[1])
        m1 = mfcc1[:, :min_len].flatten()
        m2 = mfcc2[:, :min_len].flatten()

        # Cosine similarity
        cos_sim = np.dot(m1, m2) / (np.linalg.norm(m1) * np.linalg.norm(m2))
        return round(float(cos_sim), 4)
    except Exception as e:
        return f"Error: {e}"

if __name__ == '__main__':
    # Test on two files if exist
    import sys
    if len(sys.argv) > 2:
        res = compare_audio_files(sys.argv[1], sys.argv[2])
        print(f"Spectral Cosine Similarity: {res}")
