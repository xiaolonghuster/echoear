import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('echoear_cli', Path(__file__).parents[1] / 'echoear.py')
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)


class PackageSafetyTests(unittest.TestCase):
    def test_known_device_cannot_be_overridden(self):
        with self.assertRaises(ValueError):
            cli.validate_device('viewe', '30:ed:a0:29:a5:20', '30:ed:a0:29:a5:20')
        cli.validate_device('waveshare', '30:ED:A0:29:A5:20')
        cli.validate_device('viewe', '10:51:db:8e:d9:7c')

    def test_unknown_device_requires_matching_confirmation(self):
        with self.assertRaises(ValueError):
            cli.validate_device('viewe', 'aa:bb:cc:dd:ee:ff')
        with self.assertRaises(ValueError):
            cli.validate_device('viewe', 'aa:bb:cc:dd:ee:ff', '00:00:00:00:00:00')
        cli.validate_device('viewe', 'aa:bb:cc:dd:ee:ff', 'aa:bb:cc:dd:ee:ff')

    def test_package_identity_integrity_and_review_mode(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p / 'app.bin').write_bytes(b'firmware')
            m = {'board': cli.BOARDS['waveshare'], 'chip': 'esp32s3', 'review_mode': False,
                 'files': [{'file': 'app.bin', 'sha256': cli.digest(p / 'app.bin')}]}
            def save():
                (p / 'manifest.json').write_text(json.dumps(m))
            save()
            cli.load_package(p, 'waveshare')
            with self.assertRaises(ValueError):
                cli.load_package(p, 'viewe')
            m['review_mode'] = True
            save()
            with self.assertRaises(ValueError):
                cli.load_package(p, 'waveshare')
            m['review_mode'] = False
            save()
            (p / 'app.bin').write_bytes(b'corrupted')
            with self.assertRaises(ValueError):
                cli.load_package(p, 'waveshare')
            m['files'][0]['file'] = '../outside.bin'
            save()
            with self.assertRaises(ValueError):
                cli.load_package(p, 'waveshare')
