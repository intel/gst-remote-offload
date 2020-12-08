# This script uses the model downloader script located in the OpenVINO installation
# to download all the necessary models used in the remoteoffload tests

# model downloader Python script

if [ -z ${MODELS_PATH+x} ]
then
  echo "Error! MODELS_PATH env is not set"
  echo "Set MODELS_PATH env variable to directory where models should be populated"
  return
fi

if [ -z ${INTEL_CVSDK_DIR+x} ]
then
  echo "Error! INTEL_CVSDK_DIR env is not set"
  echo "You need to setup OpenVINO environment. ex:"
  echo "source /opt/intel/openvino/bin/setupvars.sh"
  return
fi


modelDownloader=${INTEL_CVSDK_DIR}/deployment_tools/tools/model_downloader/downloader.py

modelDir=${MODELS_PATH}

# prefix to see output from this script
prefix="[get_models.sh]:"

echo "${prefix}Models will be downloaded to: $modelDir"

modName=face-detection-adas-0001*
echo "${prefix}Downloading model $modName"
$modelDownloader --name $modName --output_dir $modelDir

modName=age-gender-recognition-retail-0013*
echo "${prefix}Downloading model $modName"
$modelDownloader --name $modName --output_dir $modelDir

modName=emotions-recognition-retail-0003*
echo "${prefix}Downloading model $modName"
$modelDownloader --name $modName --output_dir $modelDir

modName=face-detection-retail-0004*
echo "${prefix}Downloading model $modName"
$modelDownloader --name $modName --output_dir $modelDir

modName=mobilenet-ssd*
echo "${prefix}Downloading model $modName"
$modelDownloader --name $modName --output_dir $modelDir
python3 ${INTEL_CVSDK_DIR}/deployment_tools/model_optimizer/mo.py \
 --input_model $modelDir/public/mobilenet-ssd/mobilenet-ssd.caffemodel \
 --output_dir $modelDir/public/mobilenet-ssd/mobilenet-ssd/

modName=vehicle-license-plate-detection-barrier-0106*
echo "${prefix}Downloading model $modName"
$modelDownloader --name $modName --output_dir $modelDir

modName=vehicle-attributes-recognition-barrier-0039*
echo "${prefix}Downloading model $modName"
$modelDownloader --name $modName --output_dir $modelDir

modName=license-plate-recognition-barrier-0001*
echo "${prefix}Downloading model $modName"
$modelDownloader --name $modName --output_dir $modelDir

modName=person-vehicle-bike-detection-crossroad-0078*
echo "${prefix}Downloading model $modName"
$modelDownloader --name $modName --output_dir $modelDir

#modName=person-attributes-recognition-crossroad-0200*
#echo "${prefix}Downloading model $modName"
#$modelDownloader --name $modName --output_dir $modelDir



echo "${prefix}: Models have been downloaded to: $modelDir"
for f in `find $modelDir -name "*.xml"`; do
	echo "${prefix}Downloaded model: $f"
done

