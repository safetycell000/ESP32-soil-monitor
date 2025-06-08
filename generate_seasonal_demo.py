#!/usr/bin/env python3
import json
import datetime
import random
import math

def get_seasonal_decay_rate(month, day):
    """季節による減少速度を計算"""
    
    # 季節別基本減少率（デモ用に誇張）
    seasonal_rates = {
        1: 0.6,   # 冬：遅い（湿度保持）
        2: 0.5,   # 冬：遅い（湿度保持）
        3: 0.8,   # 春：普通
        4: 0.9,   # 春：やや速い
        5: 1.0,   # 初夏：普通
        6: 0.4,   # 梅雨：非常に遅い（湿気多い）
        7: 1.8,   # 夏：非常に速い（猛暑・乾燥）
        8: 1.9,   # 夏：最も速い（猛暑ピーク）
        9: 1.4,   # 初秋：速い（残暑）
        10: 1.1,  # 秋：やや速い
        11: 0.9,  # 晩秋：普通
        12: 0.7   # 冬：遅い（寒く湿度保持）
    }
    
    base_rate = seasonal_rates[month]
    
    # 月内での微調整（現実感を出すため）
    if month == 6:  # 梅雨
        # 月初は普通、中旬〜下旬は非常に遅い
        if day <= 10:
            rate = 0.8
        elif day <= 20:
            rate = 0.3  # 梅雨ピーク
        else:
            rate = 0.4
    elif month == 7:  # 夏
        # 月末にかけて更に暑くなる
        rate = base_rate + (day / 31.0) * 0.3
    elif month == 8:  # 夏ピーク
        # 月中が最も暑い
        heat_factor = 1.0 - abs(day - 15) / 15.0  # 15日が中心
        rate = base_rate + heat_factor * 0.4
    else:
        # その他の月は基本値に少し変動
        rate = base_rate + random.uniform(-0.1, 0.1)
    
    return max(0.2, rate)  # 最低値制限

def get_watering_effectiveness(month, current_moisture):
    """月別の給水効果を計算"""
    
    # 季節別給水効果（現実的な要因）
    seasonal_effectiveness = {
        1: 1.0,   # 冬：普通
        2: 1.0,   # 冬：普通
        3: 1.1,   # 春：良好
        4: 1.2,   # 春：良好
        5: 1.0,   # 初夏：普通
        6: 0.8,   # 梅雨：効果減（既に湿っている）
        7: 1.3,   # 夏：効果大（乾燥しているので浸透良い）
        8: 1.4,   # 夏：効果大（乾燥しているので浸透良い）
        9: 1.2,   # 初秋：良好
        10: 1.1,  # 秋：良好
        11: 1.0,  # 晩秋：普通
        12: 0.9   # 冬：やや効果減（土が固い）
    }
    
    base_effectiveness = seasonal_effectiveness[month]
    
    # 現在の湿度による効果調整（飽和効果）
    saturation_factor = 1.0 - (current_moisture / 100.0)  # 湿度が高いほど効果減
    saturation_factor = max(0.3, saturation_factor)  # 最低30%は効果あり
    
    return base_effectiveness * saturation_factor

def generate_seasonal_demo_data():
    """季節変化を明確にした検証用デモデータ"""
    
    data = []
    current_moisture = 70.0  # 適度な初期値
    
    print("🌱 季節別減少速度デモデータ生成中...")
    print("📊 月別設定:")
    
    # 月別の想定結果を表示
    month_info = {
        1: "❄️  冬：減少遅い(0.6) → 給水効果で上昇傾向",
        2: "❄️  冬：減少遅い(0.5) → 給水効果で上昇傾向", 
        3: "🌸 春：減少普通(0.8) → バランス",
        4: "🌸 春：減少やや速い(0.9) → ほぼバランス",
        5: "🌿 初夏：減少普通(1.0) → バランス",
        6: "☔ 梅雨：減少非常に遅い(0.4) → 大幅上昇",
        7: "🌞 夏：減少非常に速い(1.8) → 下降傾向",
        8: "🔥 夏：減少最速(1.9) → 大幅下降",
        9: "🍂 初秋：減少速い(1.4) → 下降傾向",
        10: "🍂 秋：減少やや速い(1.1) → わずか下降",
        11: "🍁 晩秋：減少普通(0.9) → ほぼバランス",
        12: "❄️  冬：減少遅い(0.7) → 上昇傾向"
    }
    
    for month, info in month_info.items():
        print(f"  {month:2d}月: {info}")
    
    for month in range(1, 13):
        # その月の日数
        if month == 2:
            days_in_month = 29
        elif month in [4, 6, 9, 11]:
            days_in_month = 30
        else:
            days_in_month = 31
        
        month_start_moisture = current_moisture
        
        for day in range(1, days_in_month + 1):
            # その日の減少速度を取得
            daily_decay_rate = get_seasonal_decay_rate(month, day)
            
            # 1日48回のデータ（30分間隔）
            for hour in range(24):
                for minutes in [0, 30]:
                    timestamp = datetime.datetime(2024, month, day, hour, minutes, 0)
                    
                    # 給水判定（3時間ごと）
                    is_watering = (hour % 3 == 0 and minutes == 0)
                    
                    if is_watering:
                        # 給水時：季節と現在湿度に応じた効果
                        base_increase = 12.0  # 基本給水効果
                        effectiveness = get_watering_effectiveness(month, current_moisture)
                        
                        actual_increase = base_increase * effectiveness + random.uniform(-1, 1)
                        current_moisture += actual_increase
                        
                    else:
                        # 非給水時：季節による減少
                        base_decrease = 1.2  # デモ用に速めの基本減少（30分で1.2%）
                        
                        # 現在の湿度による蒸発係数（高いほど蒸発しやすい）
                        moisture_factor = 0.7 + (current_moisture / 100.0) * 0.5
                        
                        actual_decrease = base_decrease * daily_decay_rate * moisture_factor
                        actual_decrease += random.uniform(-0.1, 0.1)
                        
                        current_moisture -= actual_decrease
                    
                    # 物理的制限
                    current_moisture = max(20.0, min(90.0, current_moisture))
                    current_moisture = round(current_moisture, 1)
                    
                    # raw_value計算
                    raw_value = int(3200 - (current_moisture / 100) * (3200 - 1200))
                    
                    data.append({
                        "timestamp": timestamp.strftime("%Y-%m-%dT%H:%M:%SZ"),
                        "moisture_percent": current_moisture,
                        "raw_value": raw_value,
                        "is_watering": is_watering
                    })
        
        month_end_moisture = current_moisture
        month_change = month_end_moisture - month_start_moisture
        print(f"  {month:2d}月実績: {month_start_moisture:5.1f}% → {month_end_moisture:5.1f}% (変化{month_change:+5.1f}%)")
    
    return data

def analyze_seasonal_demo(data):
    """季節デモデータの分析"""
    print("\\n📊 季節変化検証結果:")
    print("")
    
    for month in range(1, 13):
        month_data = [d for d in data if d['timestamp'].startswith(f'2024-{month:02d}')]
        
        if month_data:
            first_value = month_data[0]['moisture_percent']
            last_value = month_data[-1]['moisture_percent']
            month_change = last_value - first_value
            
            # 給水効果の平均を計算
            watering_effects = []
            decay_rates = []
            
            for i in range(len(month_data) - 1):
                if month_data[i]['is_watering']:
                    # 給水効果
                    if i + 1 < len(month_data):
                        effect = month_data[i+1]['moisture_percent'] - month_data[i]['moisture_percent']
                        watering_effects.append(effect)
                else:
                    # 減少率
                    if i + 1 < len(month_data) and not month_data[i+1]['is_watering']:
                        decay = month_data[i]['moisture_percent'] - month_data[i+1]['moisture_percent']
                        if decay > 0:  # 減少した場合のみ
                            decay_rates.append(decay)
            
            avg_watering_effect = sum(watering_effects) / len(watering_effects) if watering_effects else 0
            avg_decay_rate = sum(decay_rates) / len(decay_rates) if decay_rates else 0
            
            # 傾向判定
            if month_change > 5:
                trend = "🔴 上昇過剰"
            elif month_change > 1:
                trend = "🟢 上昇"
            elif month_change > -1:
                trend = "🟡 安定"
            elif month_change > -5:
                trend = "🟠 下降"
            else:
                trend = "🔴 下降過剰"
            
            seasons = {
                1: "❄️", 2: "❄️", 3: "🌸", 4: "🌸", 5: "🌿", 6: "☔",
                7: "🌞", 8: "🔥", 9: "🍂", 10: "🍂", 11: "🍁", 12: "❄️"
            }
            
            print(f"{seasons[month]} {month:2d}月: {first_value:5.1f}% → {last_value:5.1f}% (変化{month_change:+5.1f}%)")
            print(f"      給水効果{avg_watering_effect:+4.1f}% | 減少率{avg_decay_rate:4.1f}% | {trend}")
            print("")

def main():
    """メイン処理"""
    print("🌱 季節変化デモンストレーションデータ生成")
    print("=" * 50)
    
    # データ生成
    data = generate_seasonal_demo_data()
    
    # メタデータ
    metadata = {
        "device_id": "ESP32-S3-001",
        "sensor_type": "capacitive_soil_moisture",
        "watering_interval_hours": 3,
        "watering_duration_seconds": 10,
        "data_interval_minutes": 30,
        "calibration": {
            "dry_value": 3200,
            "wet_value": 1200,
            "last_calibration": "2024-01-01T00:00:00Z"
        },
        "demo_parameters": {
            "base_decay_rate": "1.2%/30min（デモ用に高速化）",
            "winter_decay": "0.5-0.7倍（湿度保持）",
            "rainy_decay": "0.3-0.4倍（梅雨）", 
            "summer_decay": "1.8-1.9倍（猛暑乾燥）",
            "watering_effect": "季節・湿度依存（8-16%）",
            "expected_trends": {
                "winter": "給水効果 > 減少 → 上昇",
                "rainy": "給水効果 >> 減少 → 大幅上昇",
                "summer": "給水効果 < 減少 → 下降",
                "other": "給水効果 ≈ 減少 → 安定"
            }
        }
    }
    
    # JSONファイル作成
    full_data = {
        "metadata": metadata,
        "data": data
    }
    
    with open('yearly-data-final.json', 'w', encoding='utf-8') as f:
        json.dump(full_data, f, ensure_ascii=False, indent=2)
    
    print(f"\\n✅ 季節デモデータ生成完了！")
    print(f"📊 総データ数: {len(data):,} 件")
    print(f"📅 期間: 2024年1月1日 〜 2024年12月31日")
    print(f"💾 ファイル: yearly-data-final.json")
    
    # 分析実行
    analyze_seasonal_demo(data)
    
    print("🎯 検証ポイント:")
    print("  1. 冬（1,2,12月）：上昇傾向（減少遅い）")
    print("  2. 梅雨（6月）：大幅上昇（減少極遅）")
    print("  3. 夏（7,8月）：下降傾向（減少極速）")
    print("  4. その他：安定〜軽微変動")

if __name__ == "__main__":
    main()