const fs = require('fs')
const path = require('path')
const execSync = require('child_process').execSync

const testsuite = 'https://github.com/WebAssembly/testsuite'
const testsuiteCommitId = '5fddf13f296cb08a7a8055f4f6d63632485cab14'

execSync('rm -rf ./res/testsuite')
execSync('rm -rf ./res/spectest')

execSync(`cd ./res && git clone ${testsuite}`)
execSync(
    `cd ./res/testsuite && git checkout -b ${testsuiteCommitId} ${testsuiteCommitId}`
)
execSync('cd ./res/testsuite && mkdir spectest')

fs.readdirSync('./res/testsuite').forEach((file) => {
    const filePath = path.resolve('./res/testsuite', file)
    const fileStat = fs.statSync(filePath)

    if (fileStat.isFile() && file.endsWith('.wast')) {
        const fileBaseName = file.slice(0, file.length - 5)
        fs.mkdirSync(path.resolve('./res/testsuite/spectest/', fileBaseName))
        execSync(
            `cd ./res/testsuite && wast2json ${file} -o ./spectest/${fileBaseName}/${fileBaseName}.json`
        )
    }
})

execSync('mv ./res/testsuite/spectest ./res/spectest')
