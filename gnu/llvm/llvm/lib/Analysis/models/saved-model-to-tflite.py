"""Convert a saved model to tflite model.

Usage: python3 saved-model-to-tflite.py <mlgo saved_model_dir> <tflite dest_dir>

The <tflite dest_dir> will contain:
  model.tflite: this is the converted saved model
  output_spec.json: the output spec, copied from the saved_model dir.
"""

import tensorflow as tf
import os
import sys
from tf_agents.policies import greedy_policy


def main(argv):
    assert len(argv) == 3
    sm_dir = argv[1]
    tfl_dir = argv[2]
    tf.io.gfile.makedirs(tfl_dir)
    tfl_path = os.path.join(tfl_dir, "model.tflite")
    converter = tf.lite.TFLiteConverter.from_saved_model(sm_dir)
    converter.target_spec.supported_ops = [
        tf.lite.OpsSet.TFLITE_BUILTINS,
    ]
    tfl_model = converter.convert()
    with tf.io.gfile.GFile(tfl_path, "wb") as f:
        f.write(tfl_model)

    json_file = "output_spec.json"
    src_json = os.path.join(sm_dir, json_file)
    if tf.io.gfile.exists(src_json):
        tf.io.gfile.copy(src_json, os.path.join(tfl_dir, json_file))


if __name__ == "__main__":
    main(sys.argv)
