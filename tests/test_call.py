import unittest

import jep
Test = jep.findClass('jep.Test')

class TestTypes(unittest.TestCase):
    def setUp(self):
        self.test = Test()

    def test_string(self):
        self.assertEqual("toString(). Thanks for calling Java(tm).", self.test.toString())

    def test_enum(self):
        testEnum = self.test.getEnum()
        self.assertEquals(0, testEnum.ordinal())

    def test_long(self):
        self.assertEquals(9223372036854775807, self.test.getClassLong().longValue())

    def test_double(self):
        self.assertEquals(4.9E-324, self.test.getClassDouble().doubleValue())

    def test_float(self):
        self.assertAlmostEqual(3.4028234663852886e+38, self.test.getClassFloat().floatValue())

    def test_intobj(self):
        self.assertEquals(-2147483648, self.test.getInteger().intValue())

    def test_getobj(self):
        obj = self.test.getObject()
        self.assertEquals("list 0", str(obj.get(0)))

    def test_getstring_array(self):
        obj = self.test.getStringArray()
        self.assertEquals('one', obj[0])
        self.assertEquals('two', obj[1])
        self.assertEquals('one two', ' '.join(obj))

    def test_string_string_array(self):
        obj = self.test.getStringStringArray()
        self.assertEquals('one', obj[0][0])

    def test_int_array(self):
        obj = self.test.getIntArray()
        self.assertEquals(1, obj[0])

    def test_bool_array(self):
        obj = self.test.getBooleanArray()
        self.assertTrue(obj[1])

    def test_short_array(self):
        obj = self.test.getShortArray()
        self.assertEquals(123, obj[0])

    def test_float_array(self):
        obj = self.test.getFloatArray()
        self.assertAlmostEquals(123.12300109863281, obj[0])

    def test_object_array(self):
        obj = self.test.getObjectArray()
        self.assertEquals(self.test.toString(), obj[0].toString())

    def test_equals(self):
        self.assertTrue(self.test.getClass() == Test)
        from java.lang import Class, String, Integer
        self.assertFalse(self.test.getClass() == Class)
        self.assertEqual(String('one'), String('one'))
        self.assertTrue(String('1') == String('1'))
        self.assertEqual(self.test, self.test)
        self.assertNotEqual(self.test, Test())
        self.assertNotEqual(String('two'), String('one'))

        self.assertEqual(String, String)
        self.assertNotEqual(String, Integer)
