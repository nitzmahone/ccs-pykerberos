# -*- coding: utf-8 -*-

import winrm

p = winrm.Protocol(endpoint='http://member1.ansible.vagrant:8888/wsman', transport='kerberos', username='testguy@ANSIBLE.VAGRANT', message_encryption='always')

r = p.send_message('<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:wsmid="http://schemas.dmtf.org/wbem/wsman/identity/1/wsmanidentity.xsd"><s:Header/><s:Body><wsmid:Identify/></s:Body></s:Envelope>')

pass

# import kerberos
# from requests_kerberos import HTTPKerberosAuth, REQUIRED
# import requests
# import requests.auth
# import time
# import os
# import re
# import sys
#
# endpoint = 'http://member1.ansible.vagrant:5985/wsman'
#
# # TODO: shouldn't need preemptive at false
# auth = HTTPKerberosAuth(mutual_authentication=REQUIRED, force_preemptive=True)
#
# session = requests.Session()
# session.auth = auth
# session.headers.update({'Content-Type': 'application/soap+xml;charset=UTF-8'})
# session.trust_env = True
# settings = session.merge_environment_settings(url=endpoint, proxies={'http':'http://192.168.33.51:8888/'}, stream=None,
#                                               verify=None, cert=None)
#
# # we're only applying proxies from env, other settings are ignored
# session.proxies = settings['proxies']
#
# req = requests.Request('POST', endpoint)
# preq = session.prepare_request(req)
# resp = session.send(preq)
# resp.raise_for_status()
#
# input = '<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:wsmid="http://schemas.dmtf.org/wbem/wsman/identity/1/wsmanidentity.xsd"><s:Header/><s:Body><wsmid:Identify/></s:Body></s:Envelope>'
# #input = '<?xml version="1.0" encoding="UTF-8"?><env:Envelope xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:env="http://www.w3.org/2003/05/soap-envelope" xmlns:a="http://schemas.xmlsoap.org/ws/2004/08/addressing" xmlns:b="http://schemas.dmtf.org/wbem/wsman/1/cimbinding.xsd" xmlns:n="http://schemas.xmlsoap.org/ws/2004/09/enumeration" xmlns:x="http://schemas.xmlsoap.org/ws/2004/09/transfer" xmlns:w="http://schemas.dmtf.org/wbem/wsman/1/wsman.xsd" xmlns:p="http://schemas.microsoft.com/wbem/wsman/1/wsman.xsd" xmlns:rsp="http://schemas.microsoft.com/wbem/wsman/1/windows/shell" xmlns:cfg="http://schemas.microsoft.com/wbem/wsman/1/config"><env:Header><a:To>http://member1.ansible.vagrant:8888/wsman</a:To><a:ReplyTo><a:Address mustUnderstand="true">http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:Address></a:ReplyTo><w:MaxEnvelopeSize mustUnderstand="true">153600</w:MaxEnvelopeSize><a:MessageID>uuid:4039361D-B814-4037-86B5-C8A88D5EDB67</a:MessageID><p:SessionId mustUnderstand="false">uuid:02F30A6D-3265-44F9-AE9F-948E5256FC74</p:SessionId><w:Locale xml:lang="en-US" mustUnderstand="false"/><p:DataLocale xml:lang="en-US" mustUnderstand="false"/><w:OperationTimeout>PT60S</w:OperationTimeout><w:ResourceURI mustUnderstand="true">http://schemas.microsoft.com/wbem/wsman/1/wmi/root/cimv2/*</w:ResourceURI><a:Action mustUnderstand="true">http://schemas.xmlsoap.org/ws/2004/09/enumeration/Enumerate</a:Action></env:Header><env:Body><n:Enumerate><w:OptimizeEnumeration xsi:nil="true"/><w:MaxElements>32000</w:MaxElements><w:Filter Dialect="http://schemas.microsoft.com/wbem/wsman/1/WQL">select * from Win32_OperatingSystem</w:Filter><p:SessionId>uuid:02F30A6D-3265-44F9-AE9F-948E5256FC74</p:SessionId></n:Enumerate></env:Body></env:Envelope>'
#
# encdata, sig = auth.wrap('member1.ansible.vagrant', input)
#
# req = requests.Request('POST', endpoint)
# req.headers['Content-Type'] = 'multipart/encrypted;protocol="application/HTTP-Kerberos-session-encrypted";boundary="Encrypted Boundary"'
# req.data = ("""--Encrypted Boundary\r
# Content-Type: application/HTTP-Kerberos-session-encrypted\r
# OriginalContent: type=application/soap+xml;charset=UTF-8;Length={0}\r
# --Encrypted Boundary\r
# Content-Type: application/octet-stream\r
# {1}--Encrypted Boundary--\r
# """).format(len(input), encdata) # TODO: need to account for extra padding if present
#
# preq = session.prepare_request(req)
# resp = session.send(preq)
# resp.raise_for_status()
#
# enc_match = re.search('(?ms)--Encrypted Boundary[\r\n]+Content-Type: application/octet-stream[\r\n]+(.+)--Encrypted Boundary--', resp.content)
# if not enc_match:
#     raise Exception("Malformed server response, couldn't find encrypted boundary")
#
# enc_resp = enc_match.group(1)
#
# y = auth.unwrap('member1.ansible.vagrant', enc_resp)
#
# # TODO: check for multipart-encrypted response content type
# # check first encrypted boundary for application/HTTP-Kerberos-session-encrypted Content-Type + OriginalContent application/soap+xml, charset
# # check second encrypted boundary for application/octet-stream
#
#
# print "result was %s" % y
#
#
#
#
#
