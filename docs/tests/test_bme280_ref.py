"""Referência em ponto flutuante (fórmulas do datasheet BME280, seção 4.2.3) e comparação
com a saída do test_bme280_comp.c. Uso: python3 test_bme280_ref.py"""
import subprocess, sys, os, tempfile
here = os.path.dirname(os.path.abspath(__file__))
exe = os.path.join(tempfile.mkdtemp(), "t_bme")
core = os.path.join(here, "..", "firmware", "Core")
subprocess.run(["gcc", "-std=c11", "-Wall", "-I", os.path.join(core, "Src"), "-I", os.path.join(core, "Inc"), "-o", exe,
                os.path.join(here, "test_bme280_comp.c")], check=True)
out = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.split("\n")
T1,T2,T3 = 27504,26435,-1000
P = [36477,-10685,3024,2855,140,-7,15500,-14600,6000]
H1,H2,H3,H4,H5,H6 = 75,366,0,317,50,30
def ref(adc_t, adc_p, adc_h):
    v1=(adc_t/16384.0-T1/1024.0)*T2
    v2=((adc_t/131072.0-T1/8192.0)**2)*T3
    tf=v1+v2; T=tf/5120.0
    v1=tf/2.0-64000.0; v2=v1*v1*P[5]/32768.0; v2=v2+v1*P[4]*2.0; v2=v2/4.0+P[3]*65536.0
    v1=(P[2]*v1*v1/524288.0+P[1]*v1)/524288.0; v1=(1.0+v1/32768.0)*P[0]
    p=1048576.0-adc_p; p=(p-v2/4096.0)*6250.0/v1
    v1=P[8]*p*p/2147483648.0; v2=p*P[7]/32768.0; p=p+(v1+v2+P[6])/16.0
    h=tf-76800.0
    h=(adc_h-(H4*64.0+H5/16384.0*h))*(H2/65536.0*(1.0+H6/67108864.0*h*(1.0+H3/67108864.0*h)))
    h=h*(1.0-H1*h/524288.0); h=min(100.0,max(0.0,h))
    return T,p,h
ok=True
rows=[l.split() for l in out if l.strip()]
for r,(at,ap,ah) in zip(rows,[(519888,415148,30000),(500000,400000,26000),(540000,430000,36000)]):
    t=int(r[1])/100.0; p=int(r[2]); h=int(r[3])/1024.0
    rt,rp,rh=ref(at,ap,ah)
    dt,dp,dh=abs(t-rt),abs(p-rp),abs(h-rh)
    good = dt<0.02 and dp<5.0 and dh<0.1   # 32 bits: pressão com erro de até alguns Pa (~0,04 hPa), bem abaixo dos ±1 hPa do sensor
    ok &= good
    print(f"T: C={t:7.2f} ref={rt:7.2f} (d={dt:.3f})  P: C={p} ref={rp:10.2f} (d={dp:.2f})  H: C={h:6.2f} ref={rh:6.2f} (d={dh:.3f})  {'OK' if good else 'FALHOU'}")
print("RESULTADO:", "APROVADO" if ok else "REPROVADO")
sys.exit(0 if ok else 1)
