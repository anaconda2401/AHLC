import os
import sys
import io

# Ensure UTF-8 output on Windows
sys.stdout.reconfigure(encoding='utf-8')

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'flask_app'))
from app import app
from database import get_experiment

client = app.test_client()

# We test with the synthetic h264_with_audio.avi or file_example_AVI_1920_2_3MG.avi if present
avi_sample = os.path.join(os.path.dirname(__file__), 'video_test_artifacts', 'h264_with_audio.avi')
if not os.path.exists(avi_sample):
    # fallback to synthetic created in test suite
    print("Sample not found at", avi_sample)
    sys.exit(1)

print(f"Compressing sample: {avi_sample} (Size: {os.path.getsize(avi_sample)} bytes)")

with open(avi_sample, 'rb') as f:
    data = {'file': (io.BytesIO(f.read()), 'test_sample_audio.avi')}
    res = client.post('/compress', data=data, content_type='multipart/form-data', follow_redirects=False)

assert res.status_code == 302, f"Expected 302 redirect, got {res.status_code}"
exp_id = res.headers['Location'].split('/')[-1]
print(f"New experiment created with ID: {exp_id}")

exp = get_experiment(exp_id)
print(f"Experiment DB Record:")
print(f"  Filename: {exp['filename']}")
print(f"  Input size: {exp['original_size']} bytes")
print(f"  Output size: {exp['compressed_size']} bytes")
print(f"  Selected codec: {exp['selected_codec']}")
print(f"  Candidate codec: {exp['candidate_codec']}")
print(f"  Is fallback: {exp['is_fallback']}")
print(f"  Fallback reason: {exp['fallback_reason']}")
print(f"  AHMC container size: {exp['ahmc_container_size']}")
print(f"  Container overhead: {exp['container_overhead']}")
print(f"  Ratio: {exp['compression_ratio']}")

output_file = exp['compressed_path']
assert os.path.exists(output_file), f"Output file missing: {output_file}"
disk_size = os.path.getsize(output_file)
print(f"Actual output on disk: {output_file} ({disk_size} bytes)")
assert disk_size == exp['compressed_size'], f"Disk size {disk_size} != DB size {exp['compressed_size']}"

# Now inspect report HTML
report_res = client.get(f'/report/{exp_id}')
assert report_res.status_code == 200, f"Report failed with {report_res.status_code}"
html = report_res.data.decode('utf-8')

assert "-36" not in html and "+-" not in html and "negative" not in html.lower(), "Negative overhead found in report HTML!"
assert "ORIGINAL RETAINED (FALLBACK)" in html, "Expected 'ORIGINAL RETAINED (FALLBACK)' in HTML"
assert "No size reduction" in html, "Expected 'No size reduction' in HTML"

print("SUCCESS: New experiment report rendered cleanly without negative numbers and matches disk exactly!")
