#!/usr/bin/env python3
"""Análise dos CSVs do datalogger (LOG_NNNN.CSV).

Uso:
    python3 plot_log.py LOG_0001.CSV              # gráficos e resumo de um arquivo real
    python3 plot_log.py --sintetico exemplo.csv   # gera um CSV sintético no mesmo formato e analisa

Colunas: timestamp_ms,temp_C,hum_pct,press_hPa,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,status
status: bit 0 = BME280 ok, bit 1 = IMU ok. Campos de um sensor com falha ficam vazios.
"""
import argparse
import math
import random
import sys

import matplotlib
import pandas as pd

COLUNAS = ["timestamp_ms", "temp_C", "hum_pct", "press_hPa",
           "ax_g", "ay_g", "az_g", "gx_dps", "gy_dps", "gz_dps", "status"]


def gerar_sintetico(caminho, duracao_s=30.0, taxa_hz=208.0, seed=1):
    """Cria um CSV no formato do firmware: placa parada, depois um 'lançamento' de 3 g e um giro."""
    random.seed(seed)
    n = int(duracao_s * taxa_hz)
    linhas = [",".join(COLUNAS)]
    bme = None
    for i in range(n):
        t = i / taxa_hz
        if i % 8 == 0:  # BME280 a ~26 Hz, repetido entre leituras
            bme = (24.0 + 0.05 * t + random.gauss(0, 0.02),
                   48.0 - 0.1 * t + random.gauss(0, 0.1),
                   911.8 - 0.4 * t + random.gauss(0, 0.02))
        boost = 3.0 if 10.0 <= t < 13.0 else 0.0
        ax = random.gauss(0, 0.01)
        ay = random.gauss(0, 0.01)
        az = 1.0 + boost + random.gauss(0, 0.01)
        gz = (200.0 * math.sin(2 * math.pi * 0.5 * (t - 15))) if t >= 15 else 0.0
        gx, gy, gz = random.gauss(0, 0.05), random.gauss(0, 0.05), gz + random.gauss(0, 0.05)
        linhas.append(f"{int(t * 1000)},{bme[0]:.2f},{bme[1]:.2f},{bme[2]:.2f},"
                      f"{ax:.3f},{ay:.3f},{az:.3f},{gx:.3f},{gy:.3f},{gz:.3f},3")
    with open(caminho, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(linhas) + "\n")


def resumo(df):
    t = df["timestamp_ms"] / 1000.0
    dur = t.iloc[-1] - t.iloc[0]
    print(f"Amostras: {len(df)}   duração: {dur:.1f} s   taxa média: {len(df) / dur:.1f} Hz")
    perdas = (df["timestamp_ms"].diff() > 50).sum()
    print(f"Lacunas de timestamp > 50 ms: {perdas}")
    print(f"BME280 ok em {(df['status'] & 1 > 0).mean() * 100:.1f}% das linhas; "
          f"IMU ok em {(df['status'] & 2 > 0).mean() * 100:.1f}%")
    for col in COLUNAS[1:10]:
        s = df[col].dropna()
        if len(s):
            print(f"  {col:10s} min={s.min():9.3f}  média={s.mean():9.3f}  max={s.max():9.3f}")
    mod = (df[["ax_g", "ay_g", "az_g"]] ** 2).sum(axis=1) ** 0.5
    print(f"|a| máximo: {mod.max():.2f} g   |a| mediano: {mod.median():.3f} g")


def plotar(df, saida):
    import matplotlib.pyplot as plt
    t = df["timestamp_ms"] / 1000.0
    fig, ax = plt.subplots(4, 1, figsize=(10, 11), sharex=True)
    ax[0].plot(t, df["temp_C"], color="tab:red"); ax[0].set_ylabel("Temperatura (°C)")
    ax[1].plot(t, df["hum_pct"], color="tab:blue"); ax[1].set_ylabel("Umidade (%RH)")
    ax[2].plot(t, df["press_hPa"], color="tab:green"); ax[2].set_ylabel("Pressão (hPa)")
    for c in ("ax_g", "ay_g", "az_g"):
        ax[3].plot(t, df[c], label=c, lw=0.8)
    ax[3].set_ylabel("Aceleração (g)"); ax[3].set_xlabel("Tempo (s)"); ax[3].legend(ncol=3)
    for a in ax:
        a.grid(alpha=0.3)
    fig.tight_layout(); fig.savefig(saida, dpi=130)
    fig2, ax2 = plt.subplots(figsize=(10, 3.5))
    for c in ("gx_dps", "gy_dps", "gz_dps"):
        ax2.plot(t, df[c], label=c, lw=0.8)
    ax2.set_ylabel("Velocidade angular (°/s)"); ax2.set_xlabel("Tempo (s)"); ax2.legend(ncol=3); ax2.grid(alpha=0.3)
    fig2.tight_layout(); fig2.savefig(saida.replace(".png", "_gyro.png"), dpi=130)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", help="arquivo CSV (ou destino, com --sintetico)")
    ap.add_argument("--sintetico", action="store_true", help="gera um CSV sintético antes de analisar")
    ap.add_argument("--saida", default=None, help="PNG de saída (padrão: mesmo nome do CSV)")
    ap.add_argument("--sem-grafico", action="store_true", help="só imprime o resumo")
    args = ap.parse_args()
    matplotlib.use("Agg")
    if args.sintetico:
        gerar_sintetico(args.csv)
    df = pd.read_csv(args.csv)
    faltando = [c for c in COLUNAS if c not in df.columns]
    if faltando:
        sys.exit(f"Colunas ausentes: {faltando}")
    resumo(df)
    if not args.sem_grafico:
        saida = args.saida or args.csv.rsplit(".", 1)[0] + ".png"
        plotar(df, saida)
        print(f"Gráficos salvos em {saida} e {saida.replace('.png', '_gyro.png')}")


if __name__ == "__main__":
    main()
