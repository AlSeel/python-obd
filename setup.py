from distutils.core import setup, Extension

module1 = Extension('pyobd',sources = ['pyobd.c'])

setup (name = 'pyobd',
       version = '1.0',
       description = 'This is a obd low level interface!',
       ext_modules = [module1])
