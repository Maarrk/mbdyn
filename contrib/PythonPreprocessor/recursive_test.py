from dataclasses import dataclass
from io import StringIO
from numbers import Integral
from typing import Optional, Union
import unittest

import MBDynLib as l
import recursive as r

try:
    import pydantic
except ImportError:
    pydantic = None


class ErrprintCalled(Exception):
    """Exception raised instead of `errprint` function outputting to stderr"""
    pass


def patched_errprint(*args, **kwargs):
    output = StringIO()
    print(*args, file=output, **kwargs)
    contents = output.getvalue()
    output.close()
    raise ErrprintCalled(contents)


# put our function instead of the original one (monkeypatching)
l.errprint = patched_errprint


class TestConstDrive(unittest.TestCase):
    def test_const_drive_caller(self):
        """Check that the new module can be used the same way as the current one"""

        # just value
        cdc_l = l.ConstDriveCaller(const_value=42)
        cdc_r = r.ConstDriveCaller(const_value=42)
        self.assertEqual(cdc_r.idx, None)
        self.assertEqual(cdc_r.const_value, 42)
        self.assertEqual(str(cdc_l), str(cdc_r))

        # index and value
        cdc_l = l.ConstDriveCaller(idx=1, const_value=42)
        cdc_r = r.ConstDriveCaller(idx=1, const_value=42)
        self.assertEqual(cdc_r.idx, 1)
        self.assertEqual(cdc_r.const_value, 42)
        self.assertEqual(str(cdc_l), str(cdc_r))

        # just positional arguments (they get type hints)
        cdc_r = r.ConstDriveCaller(42, 1)
        self.assertEqual(cdc_r.idx, 1)
        self.assertEqual(cdc_r.const_value, 42)

        # create from a dictionary
        data = {'idx': 1, 'const_value': 42}
        cdc_r = r.ConstDriveCaller.from_dict(data)
        self.assertEqual(str(cdc_l), str(cdc_r))

    def test_missing_arguments(self):
        with self.assertRaises(ErrprintCalled):
            l.ConstDriveCaller()
        with self.assertRaises(ErrprintCalled):
            l.ConstDriveCaller(idx=1)
        with self.assertRaises(Exception):
            r.ConstDriveCaller()
        with self.assertRaises(Exception):
            r.ConstDriveCaller(idx=1)

    @unittest.skipIf(pydantic is None, "depends on library, since it doesn't prevent correct models from running")
    def test_wrong_argument_types(self):
        """"""
        with self.assertRaises(AssertionError):
            l.ConstDriveCaller(idx=1.0)
        with self.assertRaises(Exception):
            r.ConstDriveCaller(idx=1.0)

        with self.assertRaises(AssertionError):
            l.ConstDriveCaller(const_value='a')
        with self.assertRaises(Exception):
            r.ConstDriveCaller(const_value='a')

    def test_extra_arguments(self):
        with self.assertRaises(Exception):
            r.ConstDriveCaller(const_value=42, foo=1.0)
        # allows to catch typos in optional arguments
        with self.assertRaises(Exception):
            r.ConstDriveCaller(const_value=42, ibx=1)
        # that also prevents wrongly assigning other members in constructor
        with self.assertRaises(Exception):
            r.ConstDriveCaller(const_value=42, drive_type='bar')

    @unittest.skip('not a feature of the current library')
    def test_extra_arguments_lib(self):
        with self.assertRaises(Exception):
            l.ConstDriveCaller(const_value=42, foo=1.0)

    @unittest.skipIf(pydantic is None, "depends on library, since it doesn't prevent correct models from running")
    def test_schema(self):
        pydantic.TypeAdapter(r.ConstDriveCaller).json_schema()

    def test_abstract_class(self):
        """Check that user can't create abstract classes, which are only used to share functionality"""
        with self.assertRaises(TypeError):
            e = r._MBEntity()
        with self.assertRaises(TypeError):
            dc = r.DriveCaller()

    def test_missing_redefine(self):
        """Check that the library developer is forced to redefine attributes with `@redefine_default`"""
        with self.assertRaises(TypeError):
            @dataclass
            class BadDriveCaller(r.DriveCaller):
                def drive_type(self) -> str:
                    return "bad"

                useless: int
                values: int = 0
                # The class will be valid (the test will fail) if you uncomment the line below
                # idx: Optional[Union[r.MBVar, Integral]] = None

                def __str__(self):
                    return f'''{self.drive_header()}, {self.useless}, {self.values}'''

            bad = BadDriveCaller(1, 2)


if __name__ == '__main__':
    unittest.main()
